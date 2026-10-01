/*
 * H5/AI 会话状态机。
 *
 *                 H5 入站
 *   WAITING ----------------------> H5_ACTIVE
 *      ^                               |
 *      | 断连/AI 结束/失败             | AI start 抢占
 *      |                               v
 *      +------ AI_ACTIVE <------ AI_CONNECTING
 *                    start_session 成功
 *
 * s_task 是状态机唯一写入者。TiRTC、MQTT 和 HTTP 回调只把有界事件投递到
 * s_queue；这样连接互斥、超时、资源释放和迟到回调过滤都在一个任务内顺序执行。
 * 对外状态使用原子快照，供串口或产品 UI 无锁读取。
 */
#include "starter_runtime.h"

#include <stdatomic.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "cJSON.h"
#include "esp_log.h"
#include "esp_random.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "platform_client.h"
#include "runtime_config.h"
#include "starter_media.h"
#include "starter_tirtc.h"

#define RUNTIME_QUEUE_DEPTH 6U
#define RUNTIME_TEXT_MAX 4096U
#define AI_COMMAND 0x2100U
#define AI_REQUEST_TIMEOUT_MS 17000
#define AI_CONNECT_TIMEOUT_MS 12000
#define AI_RESPONSE_TIMEOUT_MS 10000
#define AI_START_SETTLE_MS 300

/* 所有异步来源都归一成事件，由 runtime_task 串行处理。 */
typedef enum {
    EVENT_TIRTC_STATE = 0, /* SDK 启停通知。 */
    EVENT_CONNECTION,      /* H5 入站或 AI 外连的连接结果。 */
    EVENT_COMMAND,         /* TiRTC 控制命令，当前用于 AI JSON-RPC。 */
    EVENT_AI_START,        /* 产品控制意图。 */
    EVENT_AI_STOP,         /* 产品控制意图。 */
    EVENT_AI_TOKEN,        /* /v1/ai/token 的异步响应。 */
    EVENT_PLATFORM_SIGNAL, /* MQTT 设备信令，例如 unbind。 */
} runtime_event_type_t;

/*
 * 固定大小的事件头进入 FreeRTOS 队列。text 在回调中按需复制到堆上，所有权
 * 随事件转移给 runtime_task，并由 release_event() 统一释放。
 */
typedef struct {
    runtime_event_type_t type;  /* 选择事件解释方式。 */
    starter_tirtc_mode_t mode;  /* 事件所属 H5/AI 模式。 */
    uint32_t generation;        /* TiRTC 连接代次。 */
    uint32_t request_tag;       /* 发起 AI 请求时的业务会话代次。 */
    uint32_t command;           /* TiRTC 命令号。 */
    uint32_t length;            /* text 有效字节数，不含结尾 NUL。 */
    bool flag;                  /* started/connected 等布尔结果。 */
    int error;                  /* ESP-IDF 或 TiRTC 错误码。 */
    char *text;                 /* 可选堆内存，消费后必须释放。 */
} runtime_event_t;

static const char *TAG = "starter_runtime";
static QueueHandle_t s_queue;
static TaskHandle_t s_task;

/* 下列字段只由 runtime_task 读写，不需要加锁。 */
static char s_device_id[65];
static char s_ai_role_id[65];
static char s_ai_request_id[24];
static int64_t s_ai_start_at_ms;
static xiaotai_runtime_t s_session;
static uint32_t s_connection_generation;

/* 供其他任务读取的公开快照，只能由 publish_state() 更新。 */
static atomic_int s_public_state;
static atomic_uint_fast32_t s_public_session_generation;
static atomic_uint_fast32_t s_public_connection_generation;
static atomic_int s_last_error;

/*
 * SDK/MQTT 回调不能阻塞等待队列。关键事件入队失败时设置恢复标志，由状态
 * 任务执行确定性的断连或重启，避免只记日志后永久停留在错误状态。
 */
static atomic_bool s_transport_recovery_required;
static atomic_bool s_platform_restart_required;

static int64_t now_ms(void)
{
    return esp_timer_get_time() / 1000;
}

static starter_runtime_state_t session_state(void)
{
    return (starter_runtime_state_t)s_session.state;
}

static uint32_t session_generation(void)
{
    return s_session.generation;
}

static void arm_session_timeout(uint32_t timeout_ms)
{
    (void)xiaotai_runtime_arm_timeout(&s_session, session_generation(),
                                      (uint32_t)now_ms(), timeout_ms);
}

static void publish_state(void)
{
    starter_runtime_state_t state = session_state();
    /* 这些原子值用于诊断快照；业务判断始终留在 runtime_task 内。 */
    atomic_store_explicit(&s_public_state, state, memory_order_release);
    atomic_store_explicit(&s_public_session_generation,
                          session_generation(),
                          memory_order_release);
    atomic_store_explicit(&s_public_connection_generation,
                          s_connection_generation,
                          memory_order_release);
    ESP_LOGI(TAG,
             "state=%s session=%lu connection=%lu",
             starter_runtime_state_name(state),
             (unsigned long)session_generation(),
             (unsigned long)s_connection_generation);
}

static bool queue_event(const runtime_event_t *event)
{
    /* 回调不能等待；队列满时由事件生产者记录并丢弃。 */
    return s_queue != NULL && event != NULL &&
           xQueueSend(s_queue, event, 0) == pdTRUE;
}

static bool copy_event_text(runtime_event_t *event,
                            const void *text,
                            size_t length)
{
    /* 加结尾 NUL 便于 JSON 解析，但 length 仍保留协议原始长度。 */
    if (event == NULL || (length > 0U && text == NULL) ||
        length > RUNTIME_TEXT_MAX) {
        return false;
    }
    event->text = malloc(length + 1U);
    if (event->text == NULL) {
        return false;
    }
    if (length > 0U) {
        memcpy(event->text, text, length);
    }
    event->text[length] = '\0';
    event->length = (uint32_t)length;
    return true;
}

static void release_event(runtime_event_t *event)
{
    if (event != NULL) {
        free(event->text);
        event->text = NULL;
    }
}

static void finish_session(int error)
{
    /* 所有退出路径汇聚到这里，确保媒体、连接、超时和 H5 门禁一起复位。 */
    starter_media_stop();
    if (starter_tirtc_connected()) {
        (void)starter_tirtc_disconnect();
    }
    starter_tirtc_accept_h5(true);
    s_connection_generation = 0;
    if (s_session.owner != XIAOTAI_OWNER_NONE) {
        (void)xiaotai_runtime_finish(&s_session, session_generation());
    }
    s_ai_start_at_ms = 0;
    s_ai_role_id[0] = '\0';
    s_ai_request_id[0] = '\0';
    atomic_store_explicit(&s_last_error, error, memory_order_release);
    publish_state();
}

static bool copy_json_string(const cJSON *object,
                             const char *name,
                             char *destination,
                             size_t capacity,
                             bool required)
{
    const cJSON *item = cJSON_IsObject(object)
                            ? cJSON_GetObjectItemCaseSensitive(object, name)
                            : NULL;
    if (!cJSON_IsString(item) || item->valuestring == NULL) {
        if (!required && destination != NULL && capacity > 0U) {
            destination[0] = '\0';
            return true;
        }
        return false;
    }
    size_t length = strlen(item->valuestring);
    if ((required && length == 0U) || length >= capacity) {
        return false;
    }
    memcpy(destination, item->valuestring, length + 1U);
    return true;
}

static void request_ai_token_response(const char *body, void *user_data)
{
    /* HTTP 回调运行在 platform_client 请求任务中，只复制响应后立即返回。 */
    runtime_event_t event = {
        .type = EVENT_AI_TOKEN,
        .request_tag = (uint32_t)(uintptr_t)user_data,
    };
    size_t length = body == NULL ? 0U : strnlen(body, RUNTIME_TEXT_MAX + 1U);
    if (length > RUNTIME_TEXT_MAX || !copy_event_text(&event, body, length)) {
        ESP_LOGE(TAG, "AI token response is too large or cannot be copied");
        return;
    }
    if (!queue_event(&event)) {
        ESP_LOGE(TAG, "AI token response dropped: runtime queue is full");
        release_event(&event);
    }
}

static void on_tirtc_started(bool started, int error, void *user_data)
{
    /* SDK 回调：只投递标量事件。 */
    (void)user_data;
    const runtime_event_t event = {
        .type = EVENT_TIRTC_STATE,
        .flag = started,
        .error = error,
    };
    if (!queue_event(&event)) {
        ESP_LOGE(TAG, "TiRTC state event dropped; scheduling session recovery");
        atomic_store_explicit(&s_transport_recovery_required,
                              true,
                              memory_order_release);
    }
}

static void on_tirtc_connection(starter_tirtc_mode_t mode,
                                uint32_t generation,
                                uint32_t request_tag,
                                bool connected,
                                int error,
                                void *user_data)
{
    /* generation 过滤连接迟到回调，request_tag 过滤 AI 请求迟到回调。 */
    (void)user_data;
    const runtime_event_t event = {
        .type = EVENT_CONNECTION,
        .mode = mode,
        .generation = generation,
        .request_tag = request_tag,
        .flag = connected,
        .error = error,
    };
    if (!queue_event(&event)) {
        ESP_LOGE(TAG, "connection event dropped; scheduling session recovery");
        atomic_store_explicit(&s_transport_recovery_required,
                              true,
                              memory_order_release);
    }
}

static void on_tirtc_command(starter_tirtc_mode_t mode,
                             uint32_t generation,
                             uint32_t command,
                             const void *data,
                             uint32_t length,
                             void *user_data)
{
    /* 命令 payload 的生命周期只到回调返回，因此需要有界复制。 */
    (void)user_data;
    if ((length > 0U && data == NULL) || length > RUNTIME_TEXT_MAX) {
        ESP_LOGW(TAG, "command 0x%lx is too large", (unsigned long)command);
        return;
    }
    runtime_event_t event = {
        .type = EVENT_COMMAND,
        .mode = mode,
        .generation = generation,
        .command = command,
    };
    if (!copy_event_text(&event, data, length)) {
        ESP_LOGE(TAG, "command 0x%lx cannot be copied", (unsigned long)command);
        return;
    }
    if (!queue_event(&event)) {
        ESP_LOGE(TAG, "command event dropped: runtime queue is full");
        release_event(&event);
    }
}

static void on_tirtc_audio(starter_tirtc_mode_t mode,
                           uint32_t generation,
                           const starter_tirtc_frame_t *frame,
                           const void *data,
                           void *user_data)
{
    (void)user_data;
    /* 媒体模块使用独立固定队列，不占用会话事件队列的有限容量。 */
    starter_media_submit_audio(mode, generation, frame, data);
}

static void on_tirtc_key_frame(uint32_t generation, void *user_data)
{
    (void)user_data;
    starter_media_request_key_frame(generation);
}

static void on_platform_signal(const char *json, size_t length, void *user_data)
{
    /* MQTT 回调和 TiRTC 回调遵守相同规则：复制、入队、立即返回。 */
    (void)user_data;
    if (json == NULL || length == 0U) {
        return;
    }
    if (length > RUNTIME_TEXT_MAX) {
        ESP_LOGE(TAG, "platform signal is oversized; scheduling safe restart");
        atomic_store_explicit(&s_platform_restart_required,
                              true,
                              memory_order_release);
        return;
    }
    runtime_event_t event = {
        .type = EVENT_PLATFORM_SIGNAL,
    };
    if (!copy_event_text(&event, json, length)) {
        ESP_LOGE(TAG, "platform signal copy failed; scheduling safe restart");
        atomic_store_explicit(&s_platform_restart_required,
                              true,
                              memory_order_release);
        return;
    }
    if (!queue_event(&event)) {
        ESP_LOGE(TAG, "platform signal dropped; scheduling safe restart");
        release_event(&event);
        atomic_store_explicit(&s_platform_restart_required,
                              true,
                              memory_order_release);
    }
}

static void begin_ai_session(void)
{
    /*
     * AI 优先于 H5：先关闭 H5 入站门禁并结束当前媒体/连接，再开启新会话代次。
     * HTTP 响应携带该代次，迟到响应不能推动后续新会话。
     */
    starter_runtime_state_t state = session_state();
    if ((state != STARTER_RUNTIME_WAITING &&
         state != STARTER_RUNTIME_H5_ACTIVE) ||
        !platform_client_ready() || !starter_tirtc_started()) {
        ESP_LOGW(TAG, "AI start ignored: platform or TiRTC is not ready");
        return;
    }

    starter_tirtc_accept_h5(false);
    starter_media_stop();
    if (starter_tirtc_connected()) {
        (void)starter_tirtc_disconnect();
    }
    s_connection_generation = 0;
    if (!xiaotai_runtime_begin(&s_session, XIAOTAI_OWNER_AI, false)) {
        return;
    }
    arm_session_timeout(AI_REQUEST_TIMEOUT_MS);
    atomic_store_explicit(&s_last_error, 0, memory_order_release);
    publish_state();

    /* MQTT bearer 由 platform_client 内部添加，状态机不接触设备密钥。 */
    esp_err_t err = platform_client_request_timeout(
        PLATFORM_SERVICE_AI,
        "/v1/ai/token",
        NULL,
        AI_REQUEST_TIMEOUT_MS - 2000U,
        request_ai_token_response,
        (void *)(uintptr_t)session_generation());
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "AI token request submission failed: %s", esp_err_to_name(err));
        finish_session(err);
    }
}

static void handle_ai_token(const runtime_event_t *event)
{
    /* 只接受当前 AI_CONNECTING 会话对应的 token 响应。 */
    if (event->request_tag != session_generation() ||
        session_state() != STARTER_RUNTIME_AI_CONNECTING) {
        return;
    }
    cJSON *root = event->length == 0U || event->text == NULL
                      ? NULL
                      : cJSON_Parse(event->text);
    const cJSON *code = root == NULL
                            ? NULL
                            : cJSON_GetObjectItemCaseSensitive(root, "code");
    const cJSON *data = root == NULL
                            ? NULL
                            : cJSON_GetObjectItemCaseSensitive(root, "data");
    char peer_id[1024];
    char token[1024];
    bool ok = cJSON_IsNumber(code) &&
              (code->valueint == 0 || code->valueint == 200) &&
              cJSON_IsObject(data) &&
              copy_json_string(data, "peer_id", peer_id, sizeof(peer_id), true) &&
              copy_json_string(data, "token", token, sizeof(token), true) &&
              copy_json_string(data,
                               "role_id",
                               s_ai_role_id,
                               sizeof(s_ai_role_id),
                               false);
    cJSON_Delete(root);
    if (!ok) {
        ESP_LOGE(TAG, "AI token response is invalid");
        finish_session(ESP_ERR_INVALID_RESPONSE);
        return;
    }
    /* 适配层立即提交连接；后续异步结果只通过 request_tag 关联本次会话。 */
    int rc = starter_tirtc_ai_connect(peer_id, token, session_generation());
    if (rc != 0) {
        ESP_LOGE(TAG, "AI connection submission failed rc=%d", rc);
        finish_session(rc);
        return;
    }
    arm_session_timeout(AI_CONNECT_TIMEOUT_MS);
}

static void send_ai_start(void)
{
    /*
     * WHIP 连接成功后发送业务层 start_session。媒体仍保持关闭，直到
     * handle_ai_command() 收到匹配 request id 的 result。
     */
    s_ai_start_at_ms = 0;
    if (!starter_tirtc_connected() || s_connection_generation == 0U) {
        finish_session(ESP_ERR_INVALID_STATE);
        return;
    }
    (void)snprintf(s_ai_request_id,
                   sizeof(s_ai_request_id),
                   "%08lx%08lx",
                   (unsigned long)esp_random(),
                   (unsigned long)esp_random());
    cJSON *root = cJSON_CreateObject();
    cJSON *params = cJSON_CreateObject();
    cJSON *input = cJSON_CreateObject();
    cJSON *output = cJSON_CreateObject();
    bool ok = root != NULL && params != NULL && input != NULL && output != NULL &&
              cJSON_AddStringToObject(root, "jsonrpc", "2.0") &&
              cJSON_AddStringToObject(root, "id", s_ai_request_id) &&
              cJSON_AddStringToObject(root, "method", "start_session") &&
              cJSON_AddStringToObject(params, "device_id", s_device_id) &&
              cJSON_AddStringToObject(params, "role_id", s_ai_role_id) &&
              cJSON_AddStringToObject(input, "codec", "alaw") &&
              cJSON_AddNumberToObject(input, "sample_rate", 8000) &&
              cJSON_AddNumberToObject(input, "channels", 1) &&
              cJSON_AddStringToObject(output, "codec", "alaw") &&
              cJSON_AddNumberToObject(output, "sample_rate", 8000) &&
              cJSON_AddNumberToObject(output, "channels", 1);
    if (ok) {
        cJSON_AddItemToObject(params, "input_audio", input);
        input = NULL;
        cJSON_AddItemToObject(params, "output_audio", output);
        output = NULL;
        cJSON_AddItemToObject(root, "params", params);
        params = NULL;
    }
    char *json = ok ? cJSON_PrintUnformatted(root) : NULL;
    cJSON_Delete(input);
    cJSON_Delete(output);
    cJSON_Delete(params);
    cJSON_Delete(root);
    if (json == NULL) {
        finish_session(ESP_ERR_NO_MEM);
        return;
    }
    int rc = starter_tirtc_send_command(AI_COMMAND, json, (uint32_t)strlen(json));
    cJSON_free(json);
    if (rc < 0) {
        ESP_LOGE(TAG, "AI start_session send failed rc=%d", rc);
        finish_session(rc);
        return;
    }
    arm_session_timeout(AI_RESPONSE_TIMEOUT_MS);
    ESP_LOGI(TAG, "AI start_session sent; audio remains stopped until accepted");
}

static void handle_connection(const runtime_event_t *event)
{
    /*
     * 连接回调可能在 disconnect 后迟到。先按 request_tag/generation 过滤，
     * 再根据当前业务状态决定接纳或关闭，避免旧连接复活。
     */
    starter_runtime_state_t state = session_state();
    if (!event->flag) {
        if (event->mode == STARTER_TIRTC_AI && event->request_tag != 0U &&
            event->request_tag != session_generation()) {
            return;
        }
        if (event->generation != 0U && s_connection_generation != 0U &&
            event->generation != s_connection_generation) {
            return;
        }
        ESP_LOGW(TAG, "connection ended mode=%d error=%d", (int)event->mode, event->error);
        finish_session(event->error);
        return;
    }

    if (event->mode == STARTER_TIRTC_H5 && state == STARTER_RUNTIME_WAITING) {
        /* H5 建连即允许媒体模块启动；实际发送还要等待远端订阅。 */
        s_connection_generation = event->generation;
        if (!xiaotai_runtime_begin(&s_session, XIAOTAI_OWNER_STREAM, false)) {
            finish_session(ESP_ERR_INVALID_STATE);
            return;
        }
        atomic_store_explicit(&s_last_error, 0, memory_order_release);
        if (starter_media_start(STARTER_TIRTC_H5, event->generation) != ESP_OK) {
            finish_session(ESP_ERR_INVALID_STATE);
            return;
        }
        (void)xiaotai_runtime_media_started(&s_session, session_generation());
        publish_state();
        return;
    }
    if (event->mode == STARTER_TIRTC_AI &&
        state == STARTER_RUNTIME_AI_CONNECTING &&
        event->request_tag == session_generation()) {
        s_connection_generation = event->generation;
        publish_state();
        /* 给底层数据通道一个短暂稳定窗口，再发 JSON-RPC 控制命令。 */
        s_ai_start_at_ms = now_ms() + AI_START_SETTLE_MS;
        arm_session_timeout(AI_RESPONSE_TIMEOUT_MS);
        return;
    }

    ESP_LOGW(TAG, "unexpected connection mode=%d; closing", (int)event->mode);
    finish_session(ESP_ERR_INVALID_STATE);
}

static bool ai_audio_format_is_alaw_8k_mono(const cJSON *format)
{
    const cJSON *codec = cJSON_IsObject(format)
                             ? cJSON_GetObjectItemCaseSensitive(format, "codec")
                             : NULL;
    const cJSON *sample_rate = cJSON_IsObject(format)
                                   ? cJSON_GetObjectItemCaseSensitive(
                                         format, "sample_rate")
                                   : NULL;
    const cJSON *channels = cJSON_IsObject(format)
                               ? cJSON_GetObjectItemCaseSensitive(format, "channels")
                               : NULL;
    return cJSON_IsString(codec) && codec->valuestring != NULL &&
           strcmp(codec->valuestring, "alaw") == 0 &&
           cJSON_IsNumber(sample_rate) && sample_rate->valueint == 8000 &&
           cJSON_IsNumber(channels) && channels->valueint == 1;
}

static void handle_ai_command(const runtime_event_t *event)
{
    /* 只消费当前 AI 连接上的 0x2100 JSON-RPC 消息。 */
    if (event->mode != STARTER_TIRTC_AI || event->command != AI_COMMAND ||
        event->generation != s_connection_generation) {
        return;
    }
    starter_runtime_state_t state = session_state();
    cJSON *root = event->text == NULL
                      ? NULL
                      : cJSON_ParseWithLength(event->text, event->length);
    const cJSON *method = root == NULL
                              ? NULL
                              : cJSON_GetObjectItemCaseSensitive(root, "method");
    if ((state == STARTER_RUNTIME_AI_CONNECTING ||
         state == STARTER_RUNTIME_AI_ACTIVE) &&
        cJSON_IsString(method) && method->valuestring != NULL &&
        strcmp(method->valuestring, "end_session") == 0) {
        cJSON_Delete(root);
        ESP_LOGI(TAG, "AI platform ended the session");
        finish_session(0);
        return;
    }
    if (state != STARTER_RUNTIME_AI_CONNECTING) {
        cJSON_Delete(root);
        return;
    }
    const cJSON *id = root == NULL
                          ? NULL
                          : cJSON_GetObjectItemCaseSensitive(root, "id");
    /* result/error 都必须属于当前 start_session，不能让无 ID 的旁路命令改状态。 */
    bool id_matches = cJSON_IsString(id) && id->valuestring != NULL &&
                      strcmp(id->valuestring, s_ai_request_id) == 0;
    const cJSON *result = root == NULL
                              ? NULL
                              : cJSON_GetObjectItemCaseSensitive(root, "result");
    const cJSON *session_id = cJSON_IsObject(result)
                                  ? cJSON_GetObjectItemCaseSensitive(result,
                                                                     "session_id")
                                  : NULL;
    const cJSON *input_audio = cJSON_IsObject(result)
                                   ? cJSON_GetObjectItemCaseSensitive(result,
                                                                      "input_audio")
                                   : NULL;
    const cJSON *output_audio = cJSON_IsObject(result)
                                    ? cJSON_GetObjectItemCaseSensitive(result,
                                                                       "output_audio")
                                    : NULL;
    bool rejected = root != NULL && id_matches &&
                    cJSON_GetObjectItemCaseSensitive(root, "error") != NULL;
    bool has_result = root != NULL && id_matches && cJSON_IsObject(result);
    bool accepted = has_result && cJSON_IsString(session_id) &&
                    session_id->valuestring != NULL &&
                    session_id->valuestring[0] != '\0' &&
                    ai_audio_format_is_alaw_8k_mono(input_audio) &&
                    ai_audio_format_is_alaw_8k_mono(output_audio);
    cJSON_Delete(root);
    if (rejected) {
        ESP_LOGE(TAG, "AI start_session was rejected");
        finish_session(ESP_FAIL);
    } else if (has_result && !accepted) {
        ESP_LOGE(TAG,
                 "AI start_session response lacks session_id or negotiated alaw/8k/mono formats");
        finish_session(ESP_ERR_INVALID_RESPONSE);
    } else if (accepted) {
        /* 服务端明确接受后才开放麦克风，避免把音频发到未建立的 AI 会话。 */
        if (starter_media_start(STARTER_TIRTC_AI,
                                s_connection_generation) != ESP_OK) {
            finish_session(ESP_ERR_INVALID_STATE);
            return;
        }
        (void)xiaotai_runtime_media_started(&s_session, session_generation());
        publish_state();
    }
}

static void end_ai_session(void)
{
    /* end_session 是尽力通知；本地资源释放不等待远端响应。 */
    starter_runtime_state_t state = session_state();
    if (state != STARTER_RUNTIME_AI_CONNECTING &&
        state != STARTER_RUNTIME_AI_ACTIVE) {
        return;
    }
    if (starter_tirtc_connected()) {
        const char end[] = "{\"jsonrpc\":\"2.0\",\"method\":\"end_session\"}";
        (void)starter_tirtc_send_command(AI_COMMAND, end, sizeof(end) - 1U);
    }
    finish_session(0);
}

static void handle_platform_signal(const runtime_event_t *event)
{
    /* unbind 清除本地凭证并重启，下一次启动重新进入验证码绑定。 */
    cJSON *root = event->text == NULL
                      ? NULL
                      : cJSON_ParseWithLength(event->text, event->length);
    const cJSON *type = root == NULL
                            ? NULL
                            : cJSON_GetObjectItemCaseSensitive(root, "type");
    bool unbound = cJSON_IsString(type) && type->valuestring != NULL &&
                   strcmp(type->valuestring, "unbind") == 0;
    cJSON_Delete(root);
    if (!unbound) {
        return;
    }
    ESP_LOGW(TAG, "device was unbound; clearing local credentials and restarting");
    finish_session(0);
    if (runtime_config_clear_tirtc() != ESP_OK) {
        ESP_LOGE(TAG, "cannot clear stored device credentials");
    }
    vTaskDelay(pdMS_TO_TICKS(300));
    esp_restart();
}

static void runtime_task(void *argument)
{
    (void)argument;
    xiaotai_runtime_init(&s_session);
    publish_state();
    for (;;) {
        /*
         * 平台信令无法可靠入队时通过重启重新核对服务端绑定状态；若设备已
         * unbind，组合根会进入签名重绑。传输事件丢失则收敛到 WAITING 并断连。
         */
        if (atomic_exchange_explicit(&s_platform_restart_required,
                                     false,
                                     memory_order_acq_rel)) {
            ESP_LOGE(TAG, "restarting after platform signal queue overflow");
            vTaskDelay(pdMS_TO_TICKS(100));
            esp_restart();
        }
        if (atomic_exchange_explicit(&s_transport_recovery_required,
                                     false,
                                     memory_order_acq_rel)) {
            ESP_LOGE(TAG, "resetting session after transport event queue overflow");
            finish_session(ESP_ERR_TIMEOUT);
        }

        /* 50 ms 轮询间隔同时用于驱动 AI 延迟发送和各阶段超时。 */
        runtime_event_t event;
        if (xQueueReceive(s_queue, &event, pdMS_TO_TICKS(50)) == pdTRUE) {
            switch (event.type) {
            case EVENT_TIRTC_STATE:
                if (!event.flag) {
                    finish_session(event.error);
                }
                break;
            case EVENT_CONNECTION:
                handle_connection(&event);
                break;
            case EVENT_COMMAND:
                handle_ai_command(&event);
                break;
            case EVENT_AI_START:
                begin_ai_session();
                break;
            case EVENT_AI_STOP:
                end_ai_session();
                break;
            case EVENT_AI_TOKEN:
                handle_ai_token(&event);
                break;
            case EVENT_PLATFORM_SIGNAL:
                handle_platform_signal(&event);
                break;
            default:
                break;
            }
            release_event(&event);
        }
        int64_t current_ms = now_ms();
        if (s_ai_start_at_ms != 0 && current_ms >= s_ai_start_at_ms) {
            send_ai_start();
        }
        if (s_session.deadline_ms != 0U &&
            (int32_t)((uint32_t)current_ms - s_session.deadline_ms) >= 0) {
            ESP_LOGW(TAG, "session setup timed out");
            finish_session(ESP_ERR_TIMEOUT);
        }
    }
}

esp_err_t starter_runtime_start(const char *device_id)
{
    if (s_task != NULL) {
        return ESP_OK;
    }
    if (device_id == NULL || device_id[0] == '\0' ||
        strlen(device_id) >= sizeof(s_device_id)) {
        return ESP_ERR_INVALID_ARG;
    }
    (void)snprintf(s_device_id, sizeof(s_device_id), "%s", device_id);
    s_queue = xQueueCreate(RUNTIME_QUEUE_DEPTH, sizeof(runtime_event_t));
    if (s_queue == NULL) {
        return ESP_ERR_NO_MEM;
    }
    /* 必须先注册回调，再由组合根启动 TiRTC，避免丢失早期启动/连接事件。 */
    const starter_tirtc_handlers_t handlers = {
        .on_started = on_tirtc_started,
        .on_connection = on_tirtc_connection,
        .on_command = on_tirtc_command,
        .on_audio = on_tirtc_audio,
        .on_key_frame = on_tirtc_key_frame,
    };
    starter_tirtc_set_handlers(&handlers);
    platform_client_set_signal_handler(on_platform_signal, NULL);
    if (xTaskCreate(runtime_task, "starter_session", 12288, NULL, 6, &s_task) != pdPASS) {
        vQueueDelete(s_queue);
        s_queue = NULL;
        return ESP_ERR_NO_MEM;
    }
    return ESP_OK;
}

static esp_err_t enqueue_simple(runtime_event_type_t type)
{
    /* 返回成功只代表控制意图已入队，状态转换结果通过 status 查询。 */
    const runtime_event_t event = {.type = type};
    return queue_event(&event) ? ESP_OK : ESP_ERR_TIMEOUT;
}

esp_err_t starter_runtime_ai_start(void)
{
    return enqueue_simple(EVENT_AI_START);
}

esp_err_t starter_runtime_ai_stop(void)
{
    return enqueue_simple(EVENT_AI_STOP);
}

starter_runtime_status_t starter_runtime_status(void)
{
    return (starter_runtime_status_t) {
        .state = (starter_runtime_state_t)atomic_load_explicit(
            &s_public_state, memory_order_acquire),
        .session_generation = (uint32_t)atomic_load_explicit(
            &s_public_session_generation, memory_order_acquire),
        .connection_generation = (uint32_t)atomic_load_explicit(
            &s_public_connection_generation, memory_order_acquire),
        .last_error = atomic_load_explicit(&s_last_error, memory_order_acquire),
    };
}

const char *starter_runtime_state_name(starter_runtime_state_t state)
{
    switch (state) {
    case STARTER_RUNTIME_WAITING: return "waiting";
    case STARTER_RUNTIME_H5_ACTIVE: return "h5-active";
    case STARTER_RUNTIME_AI_CONNECTING: return "ai-connecting";
    case STARTER_RUNTIME_AI_ACTIVE: return "ai-active";
    default: return "unknown";
    }
}
