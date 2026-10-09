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

#include <ctype.h>
#include <stdatomic.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "cJSON.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_random.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/idf_additions.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "platform_client.h"
#include "runtime_config.h"
#include "starter_media.h"
#include "starter_tirtc.h"
#include "xiaotai_ai_protocol.h"
#include "xiaotai_ai_view.h"

#define RUNTIME_QUEUE_DEPTH 12U
#define RUNTIME_TEXT_MAX 4096U
/*
 * AI token/command JSON handling and call signalling share this state-owner
 * task.  Hardware high-water telemetry reached 428 bytes with the former
 * 13 KiB stack during an ordinary AI session, so keep enough PSRAM-backed
 * headroom for the deepest supported callback chain and future SDK changes.
 */
#define RUNTIME_TASK_STACK_BYTES 24576U
#define VOIP_CONNECT_TASK_STACK_BYTES (24U * 1024U)
#define EXTERNAL_CONNECT_RESERVE_BYTES (17U * 1024U)
#define AI_PEER_ID_MAX 1024U
#define AI_TOKEN_MAX 1024U
#define AI_COMMAND 0x2100U
#define AI_REQUEST_TIMEOUT_MS 17000
#define AI_CONNECT_TIMEOUT_MS 12000
#define AI_RESPONSE_TIMEOUT_MS 10000
#define AI_START_SETTLE_MS 300
#define AI_READY_WAIT_TIMEOUT_MS 15000
#define AI_END_FINAL_AUDIO_ARRIVAL_MS 1500U
#define AI_END_DRAIN_TIMEOUT_MS 5000U
#define AI_REMOTE_CLOSE_DRAIN_TIMEOUT_MS 3000U
#define ROOM_START_SETTLE_MS 300
#define CALL_PENDING_TIMEOUT_MS 45000
#define CALL_CONNECT_TIMEOUT_MS 30000
#define VOIP_CONNECTED_WAIT_TIMEOUT_MS 35000
#define VOIP_PROFILE_RETRY_MS 15000
#define CALL_COMMAND_CONNECT 0x2000U
#define CALL_COMMAND_HANGUP 0x2001U
#define CALL_HANGUP_FLUSH_MS 120U
#define ROOM_COMMAND 0x2200U
#define ROOM_CONNECT_TIMEOUT_MS 12000
#define CONTACTS_RETRY_MS 500
#define CONTACTS_CHECK_TIMEOUT_MS 8000

/* 所有异步来源都归一成事件，由 runtime_task 串行处理。 */
typedef enum {
    EVENT_TIRTC_STATE = 0, /* SDK 启停通知。 */
    EVENT_CONNECTION,      /* H5 入站或 AI 外连的连接结果。 */
    EVENT_COMMAND,         /* TiRTC 控制命令，当前用于 AI JSON-RPC。 */
    EVENT_AI_START,        /* 产品控制意图。 */
    EVENT_AI_STOP,         /* 产品控制意图。 */
    EVENT_MAIN_KEY,
    EVENT_AI_TOKEN,        /* /v1/ai/token 的异步响应。 */
    EVENT_PLATFORM_SIGNAL, /* MQTT 设备信令，例如 unbind。 */
    EVENT_PLATFORM_ONLINE, /* MQTT 已订阅且 HTTP worker 就绪。 */
    EVENT_VOIP_PROFILE,
    EVENT_VOIP_CONNECT,
    EVENT_VOIP_CONNECT_RESULT,
    EVENT_CONTACTS_REFRESH,
    EVENT_CONTACTS_RESULT,
    EVENT_CALL_DIAL,
    EVENT_CALL_ACCEPT,
    EVENT_CALL_REJECT,
    EVENT_CALL_HANGUP,
    EVENT_CALL_MIC_MUTE,
    EVENT_CALL_CAMERA,
    EVENT_CALL_HTTP,
    EVENT_WECHAT_QUICK_CALL,
    EVENT_ROOM_ACTION,
    EVENT_ROOM_HTTP,
} runtime_event_type_t;

typedef enum {
    CALL_HTTP_DEVICE_DIAL = 1,
    CALL_HTTP_DEVICE_INFO,
    CALL_HTTP_VOIP_DIAL,
} call_http_stage_t;

typedef enum {
    ROOM_ACTION_SYNC = 1,
    ROOM_ACTION_CREATE,
    ROOM_ACTION_JOIN,
    ROOM_ACTION_LEAVE,
    ROOM_ACTION_PTT,
    ROOM_ACTION_FOREGROUND,
} room_action_t;

typedef enum {
    ROOM_HTTP_ASSIGNMENT = 1,
    ROOM_HTTP_MUTATION,
    ROOM_HTTP_TOKEN,
    ROOM_HTTP_PRESENCE,
} room_http_stage_t;

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

/* WHIP URL 和 token 较大，不能与 TiRtcWhipConnect 的大栈帧叠加。 */
typedef struct {
    char peer_id[AI_PEER_ID_MAX];
    char token[AI_TOKEN_MAX];
} ai_credentials_t;

/* TiRtcWhipConnect 的 TLS/SDP 路径需要远大于 session 状态机的栈。
 * 请求副本由工作任务独占，取消或新会话发生时以 generation 丢弃迟到结果。 */
typedef struct {
    uint32_t generation;
    char peer_id[AI_PEER_ID_MAX];
    char token[AI_TOKEN_MAX];
} voip_connect_request_t;

static const char *TAG = "starter_runtime";
static QueueHandle_t s_queue;
static TaskHandle_t s_task;
static SemaphoreHandle_t s_product_mutex;
static starter_runtime_product_snapshot_t s_product_snapshot;
static void *s_external_connect_reserve;

/* 下列字段只由 runtime_task 读写，不需要加锁。 */
static char s_device_id[65];
static char s_ai_role_id[65];
static char s_ai_request_id[24];
static int64_t s_ai_start_at_ms;
static bool s_ai_start_pending;
static int64_t s_ai_ready_deadline_ms;
static xiaotai_ai_end_drain_t s_ai_end_drain;
static xiaotai_ai_view_t s_ai_view;
static bool s_ai_transport_closed;
static int64_t s_room_start_at_ms;
static xiaotai_runtime_t s_session;
static uint32_t s_connection_generation;
static bool s_mqtt_suspended_for_connect;
static int64_t s_mqtt_resume_due_ms;
static char s_call_room_id[129];
/* Runtime owns room/due; HTTP callbacks only complete their atomic ticket. */
static char s_call_cleanup_room[129];
static int64_t s_call_cleanup_due_ms;
static unsigned s_call_cleanup_sequence;
static atomic_uint s_call_cleanup_result;
static char s_call_peer_id[65];
static char s_call_peer_name[65];
static char s_call_wx_app_id[65];
static char s_call_wx_model_id[65];
static char s_call_connect_peer[AI_PEER_ID_MAX];
static char s_call_connect_token[AI_TOKEN_MAX];
static char s_call_wx_session_token[257];
static char s_call_wx_payload[513];
static char s_call_id[65];
static bool s_call_wechat;
#if CONFIG_IDF_TARGET_ESP32P4
static bool s_call_video;
static bool s_call_camera_enabled;
static uint16_t s_call_remote_rotation;
static bool s_call_remote_rotation_reported;
#endif
static bool s_call_outgoing;
static bool s_call_waiting_confirm;
static bool s_voip_connect_inflight;
/*
 * 设备互呼的业务接听通知（MQTT callee_answered）与 P2P 入站连接是两条
 * 独立的异步链路。不能把 0x2000 当成唯一的开通条件：该命令只是兼容性
 * 确认，可靠的建立条件是“同一代次的 P2P 已连接 + 对端已业务接听”。
 */
static bool s_call_peer_answered;
static bool s_call_p2p_connected;

#if CONFIG_IDF_TARGET_ESP32P4
static void reset_call_media_state(bool keep_pending_call)
{
    /* A pending incoming call is signaling state, not the foreground media
     * session being released. Preserve its negotiated media selection while
     * AI, H5 or Room is stopped for an explicit answer. */
    if (!keep_pending_call) {
        s_call_video = false;
        s_call_camera_enabled = false;
        s_call_remote_rotation = 0U;
        s_call_remote_rotation_reported = false;
    }
}
#endif
static bool s_voip_profile_ready;
static bool s_voip_profile_inflight;
static int64_t s_voip_profile_retry_at_ms;
static atomic_bool s_voip_profile_delivery_failed;
static bool s_wechat_quick_pending;
static bool s_contacts_inflight;
static int64_t s_contacts_retry_due_ms;
static int64_t s_contacts_check_deadline_ms;
static bool s_room_http_inflight;
static bool s_room_desired;
static bool s_room_joined;
static bool s_room_ptt;
static bool s_room_resume_pending;
static bool s_room_page_active;
static uint32_t s_room_page_epoch;
static uint32_t s_room_token_epoch;
static int64_t s_room_assignment_version;
static int64_t s_room_sync_due_ms;
static int64_t s_room_next_heartbeat_ms;
static int64_t s_room_lease_deadline_ms;
static int s_room_heartbeat_seconds = 15;
static int s_room_lease_seconds = 45;
static char s_room_id[65];
static char s_room_code[7];
static char s_room_session_id[65];
static room_action_t s_room_pending_action;
static char s_room_pending_body[320];
static room_action_t s_room_mutation_action;
static char s_room_pending_presence_body[320];
static atomic_uint s_room_http_delivery_failed_stage;
static atomic_bool s_contacts_delivery_failed;
static atomic_bool s_room_key_pressed;

/* 供其他任务读取的公开快照，只能由 publish_state() 更新。 */
static atomic_int s_public_state;
static atomic_uint_fast32_t s_public_session_generation;
static atomic_uint_fast32_t s_public_connection_generation;
static atomic_int s_last_error;

static portMUX_TYPE s_diagnostic_lock = portMUX_INITIALIZER_UNLOCKED;
static starter_runtime_diagnostics_t s_diagnostics;
static int64_t s_diagnostic_ai_started_ms;

static void diagnostic_event(const char *label, int value)
{
    starter_diagnostic_event_t event = {
        .at_ms = (uint32_t)(esp_timer_get_time() / 1000),
        .session = s_session.generation, .value = value,
    };
    (void)snprintf(event.label, sizeof(event.label), "%s", label);
    /* Only a fixed, small memory copy inside the critical section; no I/O. */
    portENTER_CRITICAL(&s_diagnostic_lock);
    if (s_diagnostics.count == STARTER_DIAGNOSTIC_EVENTS) {
        memmove(s_diagnostics.events, s_diagnostics.events + 1,
                (STARTER_DIAGNOSTIC_EVENTS - 1U) * sizeof(event));
        --s_diagnostics.count;
    }
    s_diagnostics.events[s_diagnostics.count++] = event;
    portEXIT_CRITICAL(&s_diagnostic_lock);
}

void starter_runtime_diagnostics(starter_runtime_diagnostics_t *out)
{
    if (out == NULL) return;
    portENTER_CRITICAL(&s_diagnostic_lock);
    *out = s_diagnostics;
    portEXIT_CRITICAL(&s_diagnostic_lock);
}

/*
 * SDK/MQTT 回调不能阻塞等待队列。关键事件入队失败时设置恢复标志，由状态
 * 任务执行确定性的断连或重启，避免只记日志后永久停留在错误状态。
 */
static atomic_bool s_transport_recovery_required;
static atomic_bool s_platform_restart_required;
static atomic_bool s_room_release_required;

static void room_stop_connection(const char *presence, int error);
static void room_set_foreground(bool active);
static bool room_token_is_current(void);
static void room_request_presence(const char *state);
static void room_request_token(void);

static void product_snapshot_reset(void)
{
    if (s_product_mutex == NULL ||
        xSemaphoreTake(s_product_mutex, pdMS_TO_TICKS(50)) != pdTRUE) {
        return;
    }
    starter_product_contact_t contacts[STARTER_PRODUCT_CONTACTS_MAX];
    starter_room_member_t room_members[STARTER_ROOM_MEMBERS_MAX];
    uint8_t contact_count = s_product_snapshot.contact_count;
    uint32_t contact_guide_sequence = s_product_snapshot.contact_guide_sequence;
    bool wechat_checked = s_product_snapshot.wechat_contacts_checked;
    uint8_t wechat_count = s_product_snapshot.wechat_contact_count;
    starter_room_ui_phase_t room_phase = s_product_snapshot.room_phase;
    bool room_password_set = s_product_snapshot.room_password_set;
    bool room_ptt = s_product_snapshot.room_ptt;
    uint8_t room_online_count = s_product_snapshot.room_online_count;
    uint8_t room_member_count = s_product_snapshot.room_member_count;
    bool call_incoming = xiaotai_runtime_has_incoming(&s_session) &&
                         s_product_snapshot.call_incoming;
    bool call_wechat = call_incoming && s_product_snapshot.call_wechat;
    bool call_video = call_incoming && s_product_snapshot.call_video;
    bool call_camera_enabled = call_incoming &&
                               s_product_snapshot.call_camera_enabled;
    char room_code[7];
    char room_message[49];
    char call_peer[65];
    char emotion[16];
    memcpy(contacts, s_product_snapshot.contacts, sizeof(contacts));
    memcpy(room_members, s_product_snapshot.room_members, sizeof(room_members));
    memcpy(room_code, s_product_snapshot.room_code, sizeof(room_code));
    memcpy(room_message, s_product_snapshot.room_message, sizeof(room_message));
    memcpy(call_peer, s_product_snapshot.call_peer, sizeof(call_peer));
    memcpy(emotion, s_product_snapshot.emotion, sizeof(emotion));
    s_product_snapshot = (starter_runtime_product_snapshot_t) {
        .ai_phase = STARTER_AI_UI_IDLE,
        .contact_count = contact_count,
        .contact_guide_sequence = contact_guide_sequence,
        .wechat_contacts_checked = wechat_checked,
        .wechat_contact_count = wechat_count,
        .room_phase = room_phase,
        .room_password_set = room_password_set,
        .room_ptt = room_ptt,
        .room_online_count = room_online_count,
        .room_member_count = room_member_count,
        .call_incoming = call_incoming,
        .call_wechat = call_wechat,
        .call_video = call_video,
        .call_camera_enabled = call_camera_enabled,
    };
    memcpy(s_product_snapshot.contacts, contacts, sizeof(contacts));
    memcpy(s_product_snapshot.room_members, room_members, sizeof(room_members));
    memcpy(s_product_snapshot.room_code, room_code, sizeof(room_code));
    memcpy(s_product_snapshot.room_message, room_message, sizeof(room_message));
    if (call_incoming) {
        memcpy(s_product_snapshot.call_peer, call_peer, sizeof(call_peer));
    }
    memcpy(s_product_snapshot.emotion, emotion, sizeof(emotion));
    xSemaphoreGive(s_product_mutex);
}

static void product_set_room(starter_room_ui_phase_t phase, const char *message)
{
    if (s_product_mutex != NULL &&
        xSemaphoreTake(s_product_mutex, pdMS_TO_TICKS(50)) == pdTRUE) {
        s_product_snapshot.room_phase = phase;
        s_product_snapshot.room_ptt = s_room_ptt;
        (void)snprintf(s_product_snapshot.room_code,
                       sizeof(s_product_snapshot.room_code), "%s", s_room_code);
        (void)snprintf(s_product_snapshot.room_message,
                       sizeof(s_product_snapshot.room_message), "%s",
                       message == NULL ? "" : message);
        xSemaphoreGive(s_product_mutex);
    }
}

static void product_set_call(bool incoming,
                             bool wechat,
                             const char *peer,
                             bool microphone_muted)
{
    if (s_product_mutex != NULL &&
        xSemaphoreTake(s_product_mutex, pdMS_TO_TICKS(50)) == pdTRUE) {
        s_product_snapshot.call_incoming = incoming;
        s_product_snapshot.call_wechat = wechat;
        s_product_snapshot.call_microphone_muted = microphone_muted;
#if CONFIG_IDF_TARGET_ESP32P4
        s_product_snapshot.call_video = s_call_video;
        s_product_snapshot.call_camera_enabled = s_call_camera_enabled;
#endif
        (void)snprintf(s_product_snapshot.call_peer,
                       sizeof(s_product_snapshot.call_peer),
                       "%s",
                       peer == NULL ? "" : peer);
        xSemaphoreGive(s_product_mutex);
    }
}

static void product_set_call_result(const char *result)
{
    if (result != NULL && s_product_mutex != NULL &&
        xSemaphoreTake(s_product_mutex, pdMS_TO_TICKS(50)) == pdTRUE) {
        (void)snprintf(s_product_snapshot.call_result,
                       sizeof(s_product_snapshot.call_result), "%s", result);
        xSemaphoreGive(s_product_mutex);
    }
}

static void product_set_phase(starter_ai_ui_phase_t phase)
{
    if (s_product_mutex != NULL &&
        xSemaphoreTake(s_product_mutex, pdMS_TO_TICKS(50)) == pdTRUE) {
        s_product_snapshot.ai_phase = phase;
        xSemaphoreGive(s_product_mutex);
    }
}

static void product_set_ai_start_pending(bool pending, const char *message)
{
    if (s_product_mutex != NULL &&
        xSemaphoreTake(s_product_mutex, pdMS_TO_TICKS(50)) == pdTRUE) {
        s_product_snapshot.ai_start_pending = pending;
        s_product_snapshot.caption_is_ai = false;
        s_product_snapshot.caption_final = false;
        (void)snprintf(s_product_snapshot.subtitle,
                       sizeof(s_product_snapshot.subtitle), "%s",
                       message == NULL ? "" : message);
        xSemaphoreGive(s_product_mutex);
    }
}

static bool supported_emotion(const char *emotion);
static void product_set_emotion(const char *emotion)
{
    if (!supported_emotion(emotion) || s_product_mutex == NULL ||
        xSemaphoreTake(s_product_mutex, pdMS_TO_TICKS(50)) != pdTRUE) return;
    (void)snprintf(s_product_snapshot.emotion,
                   sizeof(s_product_snapshot.emotion), "%s", emotion);
    xSemaphoreGive(s_product_mutex);
}

static bool supported_emotion(const char *emotion)
{
    static const char *const allowed[] = {
        "neutral", "happy", "laughing", "funny", "sad", "angry", "crying",
        "loving", "embarrassed", "surprised", "shocked", "thinking",
        "winking", "cool", "relaxed", "delicious", "kissy", "confident",
        "sleepy", "silly", "confused", "listening", "ambient", "speech",
        "calm", "excited", "curious", "proud", "moved",
    };
    if (emotion == NULL) {
        return false;
    }
    for (size_t i = 0; i < sizeof(allowed) / sizeof(allowed[0]); ++i) {
        if (strcmp(emotion, allowed[i]) == 0) {
            return true;
        }
    }
    return false;
}

static starter_ai_ui_phase_t product_phase_from_ai_view(xiaotai_ai_ui_phase_t phase)
{
    return phase == XIAOTAI_AI_UI_SPEAKING ? STARTER_AI_UI_SPEAKING
           : phase == XIAOTAI_AI_UI_THINKING ? STARTER_AI_UI_THINKING
                                             : STARTER_AI_UI_LISTENING;
}

static void product_set_caption(bool is_ai,
                                bool final,
                                const char *text,
                                bool append,
                                const char *emotion)
{
    if (text == NULL || s_product_mutex == NULL ||
        xSemaphoreTake(s_product_mutex, pdMS_TO_TICKS(50)) != pdTRUE) {
        return;
    }
    s_product_snapshot.caption_is_ai = is_ai;
    s_product_snapshot.caption_final = final;
    if (!append) {
        (void)snprintf(s_product_snapshot.subtitle,
                       sizeof(s_product_snapshot.subtitle),
                       "%s%s",
                       is_ai ? "AI：" : "你：",
                       text);
    } else {
        size_t used = strlen(s_product_snapshot.subtitle);
        if (used < sizeof(s_product_snapshot.subtitle) - 1U) {
            (void)snprintf(s_product_snapshot.subtitle + used,
                           sizeof(s_product_snapshot.subtitle) - used,
                           "%s",
                           text);
        }
    }
    if (supported_emotion(emotion)) {
        (void)snprintf(s_product_snapshot.emotion,
                       sizeof(s_product_snapshot.emotion),
                       "%s",
                       emotion);
    }
    xSemaphoreGive(s_product_mutex);
}

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

static bool session_incoming_pending(void)
{
    return xiaotai_runtime_has_incoming(&s_session);
}

static void arm_session_timeout(uint32_t timeout_ms)
{
    if (!xiaotai_runtime_arm_timeout(&s_session, session_generation(),
                                     (uint32_t)now_ms(), timeout_ms)) {
        ESP_LOGE(TAG, "cannot arm product session timeout state=%d generation=%lu",
                 (int)session_state(), (unsigned long)session_generation());
    }
}

static void publish_state(void)
{
    starter_runtime_state_t state = session_state();
    starter_media_set_wake_allowed(state == STARTER_RUNTIME_WAITING);
    /* 这些原子值用于诊断快照；业务判断始终留在 runtime_task 内。 */
    starter_runtime_state_t previous = (starter_runtime_state_t)atomic_exchange_explicit(
        &s_public_state, state, memory_order_acq_rel);
    if (previous != state) {
        if (state == STARTER_RUNTIME_CALL_ACTIVE) diagnostic_event("call active", 0);
        /* 通话 UI 只消费该快照；保留边沿日志可直接定位是谁提前结束通话。 */
        ESP_LOGI(TAG, "runtime state %d -> %d session=%lu connection=%lu",
                 (int)previous, (int)state,
                 (unsigned long)session_generation(),
                 (unsigned long)s_connection_generation);
    }
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

static bool suspend_mqtt_for_external_connect(void)
{
    if (s_mqtt_suspended_for_connect) {
        return true;
    }
    if (s_external_connect_reserve == NULL) {
        ESP_LOGE(TAG, "external connect reserve is not armed");
        return false;
    }
    esp_err_t err = platform_client_suspend_mqtt_for_realtime();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "cannot reserve realtime connect memory: %s",
                 esp_err_to_name(err));
        return false;
    }
    /* MQTT 已完全销毁；立即交还启动期保留的连续块给 TiRtc*Connect。 */
    heap_caps_free(s_external_connect_reserve);
    s_external_connect_reserve = NULL;
    s_mqtt_suspended_for_connect = true;
    s_mqtt_resume_due_ms = 0;
    ESP_LOGI(TAG,
             "realtime connect gate opened: internal-free=%u largest=%u",
             (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL |
                                                MALLOC_CAP_8BIT),
             (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL |
                                                        MALLOC_CAP_8BIT));
    return true;
}

static void resume_mqtt_after_external_connect(void)
{
    if (!s_mqtt_suspended_for_connect) {
        return;
    }
    esp_err_t reserve_err = starter_runtime_arm_external_connect_reserve();
    if (reserve_err != ESP_OK) {
        s_mqtt_resume_due_ms = now_ms() + 1000;
        ESP_LOGW(TAG, "MQTT resume waits for external-connect reserve: %s",
                 esp_err_to_name(reserve_err));
        return;
    }
    esp_err_t err = platform_client_resume_mqtt_after_realtime();
    if (err == ESP_OK) {
        s_mqtt_suspended_for_connect = false;
        s_mqtt_resume_due_ms = 0;
        return;
    }
    s_mqtt_resume_due_ms = now_ms() + 1000;
    ESP_LOGW(TAG, "MQTT resume deferred: %s", esp_err_to_name(err));
}

esp_err_t starter_runtime_arm_external_connect_reserve(void)
{
    if (s_external_connect_reserve != NULL) {
        return ESP_OK;
    }
    s_external_connect_reserve = heap_caps_malloc(
        EXTERNAL_CONNECT_RESERVE_BYTES, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    if (s_external_connect_reserve == NULL) {
        ESP_LOGW(TAG,
                 "cannot reserve %u-byte contiguous internal heap for external connect",
                 EXTERNAL_CONNECT_RESERVE_BYTES);
        return ESP_ERR_NO_MEM;
    }
    ESP_LOGI(TAG,
             "reserved %u internal bytes for next TiRTC external connect",
             EXTERNAL_CONNECT_RESERVE_BYTES);
    return ESP_OK;
}

static void finish_session(int error)
{
    bool keep_pending_call = session_incoming_pending();
    uint32_t generation = session_generation();
    diagnostic_event("session end/error", error);
    xiaotai_ai_end_drain_cancel(&s_ai_end_drain);
    s_ai_transport_closed = false;
    /* 所有退出路径汇聚到这里，确保媒体、连接、超时和 H5 门禁一起复位。 */
    starter_media_stop();
    /* Also invalidates pending external requests / inbound call expectations.
     * No established handle is normal while cancelling a connecting session. */
    (void)starter_tirtc_disconnect();
    starter_tirtc_accept_h5(true);
    s_connection_generation = 0;
    if (s_session.owner != XIAOTAI_OWNER_NONE) {
        (void)xiaotai_runtime_finish(&s_session, generation);
    }
    s_ai_start_at_ms = 0;
    s_room_start_at_ms = 0;
    s_ai_role_id[0] = '\0';
    s_ai_request_id[0] = '\0';
    if (!keep_pending_call) {
        s_call_room_id[0] = '\0';
        s_call_peer_id[0] = '\0';
        s_call_peer_name[0] = '\0';
        s_call_wx_app_id[0] = '\0';
        s_call_wx_model_id[0] = '\0';
        s_call_connect_peer[0] = '\0';
        s_call_connect_token[0] = '\0';
        s_call_wx_session_token[0] = '\0';
        s_call_wx_payload[0] = '\0';
        s_call_id[0] = '\0';
        s_call_wechat = false;
    }
#if CONFIG_IDF_TARGET_ESP32P4
    reset_call_media_state(keep_pending_call);
#endif
    if (!keep_pending_call) s_call_outgoing = false;
    s_call_waiting_confirm = false;
    s_voip_connect_inflight = false;
    s_call_peer_answered = false;
    s_call_p2p_connected = false;
    atomic_store_explicit(&s_last_error, error, memory_order_release);
    product_snapshot_reset();
    product_set_call(keep_pending_call, keep_pending_call && s_call_wechat,
                     keep_pending_call ? s_call_peer_name : "", false);
    publish_state();
    resume_mqtt_after_external_connect();
}

static void log_ai_playback_status(const char *stage)
{
    starter_media_status_t media = starter_media_status();
    ESP_LOGI(TAG,
             "AI playback stage=%s generation=%lu rx=%lu decoded=%lu written=%lu overflow=%lu pending=%lu active=%d pcm-ms=%lu dma-ms=%lu last-rx-age-ms=%lu",
             stage, (unsigned long)session_generation(),
             (unsigned long)media.audio_received,
             (unsigned long)media.audio_decoded,
             (unsigned long)media.audio_played,
             (unsigned long)media.audio_rx_overflow,
             (unsigned long)media.audio_playback_pending,
             media.audio_playback_active ? 1 : 0,
             (unsigned long)media.audio_playback_pcm_ms,
             (unsigned long)media.audio_playback_dma_ms,
             (unsigned long)(media.audio_rx_last_ms != 0U
                 ? (uint32_t)now_ms() - media.audio_rx_last_ms : 0U));
}

static void begin_ai_end_drain(uint32_t arrival_grace_ms,
                               uint32_t timeout_ms,
                               bool transport_closed)
{
    uint32_t generation = session_generation();
    xiaotai_ai_end_drain_begin(&s_ai_end_drain,
                               generation,
                               (uint32_t)now_ms(),
                               arrival_grace_ms,
                               timeout_ms);
    s_ai_transport_closed = transport_closed;
    starter_media_set_uplink_enabled(false);
    log_ai_playback_status(transport_closed ? "transport-close" : "end-session");
    ESP_LOGI(TAG,
             "AI end_session waiting for final playback generation=%lu arrival-grace=%lu timeout=%lu transport-closed=%d",
             (unsigned long)generation,
             (unsigned long)arrival_grace_ms,
             (unsigned long)timeout_ms,
             transport_closed ? 1 : 0);
}

static void service_ai_end_drain(void)
{
    if (!s_ai_end_drain.pending) {
        return;
    }
    if (session_state() != STARTER_RUNTIME_AI_ACTIVE) {
        xiaotai_ai_end_drain_cancel(&s_ai_end_drain);
        s_ai_transport_closed = false;
        return;
    }

    starter_media_status_t media = starter_media_status();
    bool drained = media.audio_playback_pending == 0U &&
                   !media.audio_playback_active;
    uint32_t current_ms = (uint32_t)now_ms();
    bool timed_out = (int32_t)(current_ms - s_ai_end_drain.deadline_ms) >= 0;
    xiaotai_ai_end_drain_result_t result = xiaotai_ai_end_drain_step(
        &s_ai_end_drain, session_generation(), current_ms, drained);
    if (result == XIAOTAI_AI_END_DRAIN_STALE) {
        xiaotai_ai_end_drain_cancel(&s_ai_end_drain);
        s_ai_transport_closed = false;
        return;
    }
    if (result != XIAOTAI_AI_END_DRAIN_DISCONNECT) {
        return;
    }

    ESP_LOGI(TAG,
             "AI final playback %s generation=%lu pending=%lu active=%d transport-closed=%d",
             drained ? "drained" : (timed_out ? "timeout" : "complete"),
             (unsigned long)session_generation(),
             (unsigned long)media.audio_playback_pending,
             media.audio_playback_active ? 1 : 0,
             s_ai_transport_closed ? 1 : 0);
    log_ai_playback_status(drained ? "drained" : "timeout");
    finish_session(0);
}

static void call_cleanup_response(const char *body, void *user_data)
{
    unsigned ticket = (unsigned)(uintptr_t)user_data;
    cJSON *root = body != NULL ? cJSON_Parse(body) : NULL;
    const cJSON *code = cJSON_GetObjectItemCaseSensitive(root, "code");
    bool done = cJSON_IsNumber(code) &&
                (code->valueint == 0 || code->valueint == 200 || code->valueint == 40400);
    cJSON_Delete(root);
    unsigned expected = ticket;
    (void)atomic_compare_exchange_strong(&s_call_cleanup_result, &expected,
                                          ticket | (done ? 1U : 2U));
}

static void schedule_call_cleanup(const char *room)
{
    if (room == NULL || room[0] == '\0' || s_call_cleanup_room[0] != '\0') return;
    (void)snprintf(s_call_cleanup_room, sizeof(s_call_cleanup_room), "%s", room);
    s_call_cleanup_due_ms = now_ms();
    atomic_store(&s_call_cleanup_result, 0U);
}

static void service_call_cleanup(void)
{
    if (s_call_cleanup_room[0] == '\0') return;
    unsigned result = atomic_load(&s_call_cleanup_result);
    if ((result & 3U) == 1U) {
        ESP_LOGI(TAG, "call room cleanup confirmed");
        s_call_cleanup_room[0] = '\0';
        atomic_store(&s_call_cleanup_result, 0U);
        return;
    }
    if ((result & 3U) == 2U) {
        atomic_store(&s_call_cleanup_result, 0U);
        s_call_cleanup_due_ms = now_ms() + 2000;
    }
    if (now_ms() < s_call_cleanup_due_ms || !platform_client_ready()) return;
    s_call_cleanup_sequence = (s_call_cleanup_sequence + 1U) & 0x3fffffffU;
    if (s_call_cleanup_sequence == 0U) s_call_cleanup_sequence = 1U;
    unsigned ticket = s_call_cleanup_sequence << 2;
    atomic_store(&s_call_cleanup_result, ticket);
    char body[256];
    (void)snprintf(body, sizeof(body),
                   "{\"room_id\":\"%s\",\"reason\":\"session_ended\"}", s_call_cleanup_room);
    s_call_cleanup_due_ms = now_ms() + 15000;
    esp_err_t err = platform_client_request_timeout(PLATFORM_SERVICE_CALL,
        "/v1/call/hangup", body, 10000U, call_cleanup_response, (void *)(uintptr_t)ticket);
    if (err != ESP_OK) {
        atomic_store(&s_call_cleanup_result, 0U);
        s_call_cleanup_due_ms = now_ms() + 2000;
    }
}

static void finish_call_session(int error, const char *result)
{
    ESP_LOGW(TAG,
             "[DEBUG-call] finish generation=%lu error=%d result=%s",
             (unsigned long)session_generation(),
             error,
             result == NULL ? "" : result);
    if (!s_call_wechat) schedule_call_cleanup(s_call_room_id);
    finish_session(error);
    product_set_call_result(result);
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

#if CONFIG_IDF_TARGET_ESP32P4
static uint16_t remote_video_initial_rotation(bool wechat)
{
    return wechat ? 90U : 0U;
}

static void configure_remote_video_presentation(const cJSON *metadata,
                                                bool wechat)
{
    /* Device calls start upright and expose the session-local rotate control.
     * Both WeChat call directions use the same down_video_rotation contract,
     * so their P4-local downlink correction is cw90 regardless of initiator.
     * This receiver angle remains independent from uplink camera_rotation. */
    (void)metadata;
    uint16_t rotation = remote_video_initial_rotation(wechat);
    bool reported = wechat;
    const char *source = wechat ? "wechat-contract" : "device-call-default";
    s_call_remote_rotation = rotation;
    s_call_remote_rotation_reported = reported;
    starter_media_set_remote_video_presentation(rotation, reported);
    ESP_LOGI(TAG, "remote video presentation rotation=%u source=%s",
             (unsigned)rotation, source);
}
#endif

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

static bool queue_http_result(runtime_event_type_t type,
                              uint32_t request_tag,
                              uint32_t stage,
                              const char *body)
{
    runtime_event_t event = {
        .type = type,
        .request_tag = request_tag,
        .command = stage,
    };
    size_t length = body == NULL ? 0U : strnlen(body, RUNTIME_TEXT_MAX + 1U);
    if (type == EVENT_CALL_HTTP) {
        ESP_LOGI(TAG,
                 "[DEBUG-call] HTTP callback stage=%lu generation=%lu bytes=%u",
                 (unsigned long)stage,
                 (unsigned long)request_tag,
                 (unsigned)length);
    }
    if (length > RUNTIME_TEXT_MAX || !copy_event_text(&event, body, length) ||
        !queue_event(&event)) {
        ESP_LOGE(TAG, "product HTTP response dropped type=%d stage=%lu",
                 (int)type, (unsigned long)stage);
        release_event(&event);
        return false;
    }
    return true;
}

static void contacts_response(const char *body, void *user_data)
{
    (void)user_data;
    if (!queue_http_result(EVENT_CONTACTS_RESULT, 0, 0, body)) {
        atomic_store_explicit(&s_contacts_delivery_failed, true,
                              memory_order_release);
    }
}

static void voip_profile_response(const char *body, void *user_data)
{
    (void)user_data;
    if (!queue_http_result(EVENT_VOIP_PROFILE, 0, 0, body)) {
        atomic_store_explicit(&s_voip_profile_delivery_failed, true,
                              memory_order_release);
    }
}

static void call_http_response(const char *body, void *user_data)
{
    uintptr_t encoded = (uintptr_t)user_data;
    (void)queue_http_result(EVENT_CALL_HTTP,
                            (uint32_t)(encoded >> 8),
                            (uint32_t)(encoded & 0xffU),
                            body);
}

static bool call_signal_matches_room(bool got_room, const char *room,
                                     const char *current_room)
{
    return got_room && room != NULL && current_room != NULL && room[0] != '\0' &&
           current_room[0] != '\0' && strcmp(room, current_room) == 0;
}

static bool recover_voip_profile_delivery_failure(int64_t current_ms)
{
    if (!atomic_exchange_explicit(&s_voip_profile_delivery_failed, false,
                                  memory_order_acq_rel)) {
        return false;
    }
    s_voip_profile_inflight = false;
    s_voip_profile_ready = false;
    s_voip_profile_retry_at_ms = current_ms;
    return true;
}

static void *call_http_tag(uint32_t generation, call_http_stage_t stage)
{
    return (void *)(uintptr_t)(((uintptr_t)generation << 8) | (uintptr_t)stage);
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
        atomic_store_explicit(&s_transport_recovery_required, true, memory_order_release);
        return;
    }
    if (!queue_event(&event)) {
        ESP_LOGE(TAG, "command event dropped: runtime queue is full");
        release_event(&event);
        atomic_store_explicit(&s_transport_recovery_required, true, memory_order_release);
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

#if CONFIG_IDF_TARGET_ESP32P4
static void on_tirtc_video(starter_tirtc_mode_t mode, uint32_t generation,
                           const starter_tirtc_frame_t *frame, const void *data, void *ctx)
{
    (void)ctx;
    starter_media_submit_video(mode, generation, frame, data);
}
#endif

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

static void on_platform_online(void *user_data)
{
    (void)user_data;
    const runtime_event_t event = {.type = EVENT_PLATFORM_ONLINE};
    if (!queue_event(&event)) {
        ESP_LOGE(TAG, "platform online event dropped");
    }
}

static bool response_ok(const cJSON *root)
{
    const cJSON *code = cJSON_IsObject(root)
                            ? cJSON_GetObjectItemCaseSensitive(root, "code")
                            : NULL;
    return cJSON_IsNumber(code) && (code->valueint == 0 || code->valueint == 200);
}

static void dial_contact(uint8_t index, bool video);

static void refresh_contacts(void)
{
    if (s_contacts_inflight) return;
    if (!platform_client_ready()) {
        if (s_wechat_quick_pending)
            s_contacts_retry_due_ms = now_ms() + CONTACTS_RETRY_MS;
        return;
    }
    esp_err_t err = platform_client_request(PLATFORM_SERVICE_CALL,
                                             "/v1/call/device/contacts",
                                             NULL,
                                             contacts_response,
                                             NULL);
    if (err == ESP_OK) {
        s_contacts_inflight = true;
        s_contacts_retry_due_ms = 0;
    } else {
        ESP_LOGW(TAG, "contacts refresh submission failed: %s", esp_err_to_name(err));
        if (s_wechat_quick_pending)
            s_contacts_retry_due_ms = now_ms() + CONTACTS_RETRY_MS;
    }
}

static void request_device_profile(void)
{
    if (s_voip_profile_inflight || !platform_client_ready() ||
        !platform_client_mqtt_connected()) {
        return;
    }
    /* Report one complete, idempotent snapshot.  The server reads
     * profiles.voip while creating incoming and outgoing WeChat calls, so
     * this must complete after every MQTT login and before a call is accepted. */
#if CONFIG_IDF_TARGET_ESP32P4
    static const char profile[] =
        "{\"hardware\":{\"chip_model\":\"ESP32-P4\","
        "\"board_model\":\"waveshare-esp32p4-touch-lcd-43c-v10\"},"
        "\"firmware_version\":\"1.0.0+build.34\",\"profiles\":{"
        "\"stream\":{\"up_audio_streamid\":10,\"up_video_streamid\":11,"
        "\"down_audio_streamid\":10,\"down_video_streamid\":11,"
        "\"up_audio_mt\":[\"alaw\"],\"up_video_mt\":[\"h264\"],"
        "\"down_audio_mt\":[\"alaw\"],\"down_video_mt\":[\"h264\"],"
        /* Every uplink preserves sensor orientation. Scene receivers apply
         * this clockwise angle instead of spending a PPA pass on the device. */
        "\"audio_rate\":8000,\"audio_channels\":1,\"camera_rotation\":270,"
        "\"aspect_ratio\":0.75,\"hor_mirror\":false,\"vert_mirror\":false,"
        "\"object_fit\":\"contain\",\"no_video\":false},"
        "\"call\":{\"up_audio_mt\":[\"alaw\"],\"up_video_mt\":[\"h264\"],"
        "\"down_audio_mt\":[\"alaw\"],\"down_video_mt\":[\"h264\"],"
        "\"audio_rate\":8000,\"audio_channels\":1,\"camera_rotation\":270,"
        "\"aspect_ratio\":0.75,\"hor_mirror\":false,\"vert_mirror\":false,"
        "\"object_fit\":\"contain\",\"no_video\":false},"
        "\"voip\":{\"screen_width\":640,\"screen_height\":480,"
        "\"camera_rotation\":180,\"down_video_rotation\":0,"
        "\"aspect_ratio\":0.75,\"hor_mirror\":false,\"vert_mirror\":false,"
        "\"object_fit\":\"contain\",\"video_res_mode\":\"fit_screen\","
        "\"audio_rate\":8000,\"audio_channels\":1,"
        "\"up_video_mt\":\"h264\",\"down_video_mt\":\"mjpeg\","
        "\"down_audio_mt\":\"alaw\",\"no_video\":false,"
        "\"calling_timeout_sec\":30}}}";
#else
    static const char profile[] =
        "{\"hardware\":{\"chip_model\":\"ESP32-S3\","
        "\"board_model\":\"lckfb-esp32s3\"},"
        "\"firmware_version\":\"1.0.0+build.14\",\"profiles\":{"
        "\"stream\":{\"up_audio_streamid\":10,\"up_video_streamid\":11,"
        "\"down_audio_streamid\":10,\"up_video_mt\":[\"mjpeg\"],"
        "\"up_audio_mt\":[\"alaw\"],\"down_audio_mt\":[\"alaw\"],"
        "\"audio_rate\":8000,\"audio_channels\":1,\"no_video\":false},"
        "\"call\":{\"up_audio_mt\":[\"alaw\"],\"down_audio_mt\":[\"alaw\"],"
        "\"audio_rate\":8000,\"audio_channels\":1,\"no_video\":true},"
        "\"voip\":{\"screen_width\":1,\"screen_height\":1,"
        "\"audio_rate\":8000,\"audio_channels\":1,"
        "\"up_video_mt\":\"none\",\"down_video_mt\":\"none\","
        "\"down_audio_mt\":\"alaw\",\"no_video\":true,"
        "\"calling_timeout_sec\":30}}}";
#endif
    esp_err_t err = platform_client_request_timeout(
        PLATFORM_SERVICE_DEVICE, "/v1/device/profile", profile,
        10000U, voip_profile_response, NULL);
    if (err == ESP_OK) {
        s_voip_profile_inflight = true;
        ESP_LOGI(TAG, "device capability submission queued (stream/call/voip)");
#if CONFIG_IDF_TARGET_ESP32P4
        ESP_LOGI(TAG, "video presentation capability: stream_rotation=270 "
                      "call_rotation=270 voip_up_rotation=0 "
                      "voip_down_rotation=90 down_rotation_mode=0");
#endif
    } else {
        s_voip_profile_retry_at_ms = now_ms() + VOIP_PROFILE_RETRY_MS;
        ESP_LOGW(TAG, "device capability submission deferred: %s", esp_err_to_name(err));
    }
}

static void handle_voip_profile(const runtime_event_t *event)
{
    s_voip_profile_inflight = false;
    cJSON *root = event->text == NULL ? NULL :
        cJSON_ParseWithLength(event->text, event->length);
    if (response_ok(root)) {
        s_voip_profile_ready = true;
        s_voip_profile_retry_at_ms = 0;
        ESP_LOGI(TAG, "device capabilities reported; WeChat calling is ready");
        cJSON_Delete(root);
        refresh_contacts();
        return;
    }
    cJSON_Delete(root);
    s_voip_profile_ready = false;
    s_voip_profile_retry_at_ms = now_ms() + VOIP_PROFILE_RETRY_MS;
    ESP_LOGW(TAG, "device capabilities rejected; retry in %u ms",
             (unsigned)VOIP_PROFILE_RETRY_MS);
}

static void handle_contacts_result(const runtime_event_t *event)
{
    s_contacts_inflight = false;
    s_contacts_retry_due_ms = 0;
    s_contacts_check_deadline_ms = 0;
    cJSON *root = event->text == NULL ? NULL :
        cJSON_ParseWithLength(event->text, event->length);
    const cJSON *data = response_ok(root)
                            ? cJSON_GetObjectItemCaseSensitive(root, "data")
                            : NULL;
    const cJSON *contacts = cJSON_IsObject(data)
                                ? cJSON_GetObjectItemCaseSensitive(data, "contacts")
                                : NULL;
    if (!cJSON_IsArray(contacts)) {
        if (s_wechat_quick_pending && s_product_mutex != NULL &&
            xSemaphoreTake(s_product_mutex, pdMS_TO_TICKS(50)) == pdTRUE) {
            s_wechat_quick_pending = false;
            s_product_snapshot.wechat_contacts_checked = true;
            s_product_snapshot.wechat_contact_count = 0;
            xSemaphoreGive(s_product_mutex);
        }
        cJSON_Delete(root);
        return;
    }
    starter_product_contact_t parsed[STARTER_PRODUCT_CONTACTS_MAX] = {0};
    starter_product_contact_t first_wechat = {0};
    uint8_t count = 0;
    uint8_t wechat_count = 0;
    const cJSON *item = NULL;
    cJSON_ArrayForEach(item, contacts) {
        const cJSON *type = cJSON_GetObjectItemCaseSensitive(item, "type");
        const cJSON *device_id = cJSON_GetObjectItemCaseSensitive(item, "device_id");
        const cJSON *remark = cJSON_GetObjectItemCaseSensitive(item, "remark");
        if (!cJSON_IsString(type) || !cJSON_IsString(device_id) ||
            device_id->valuestring == NULL || device_id->valuestring[0] == '\0') {
            continue;
        }
        starter_product_contact_t value = {0};
        starter_product_contact_t *contact = &value;
        contact->source = strcmp(type->valuestring, "voip") == 0
                              ? STARTER_CONTACT_WECHAT
                              : STARTER_CONTACT_DEVICE;
        const cJSON *online = cJSON_GetObjectItemCaseSensitive(item, "online");
        contact->online = contact->source == STARTER_CONTACT_DEVICE &&
                          cJSON_IsBool(online) && cJSON_IsTrue(online);
        (void)snprintf(contact->id, sizeof(contact->id), "%s", device_id->valuestring);
        (void)snprintf(contact->name,
                       sizeof(contact->name),
                       "%s",
                       cJSON_IsString(remark) && remark->valuestring != NULL &&
                               remark->valuestring[0] != '\0'
                           ? remark->valuestring
                           : (contact->source == STARTER_CONTACT_WECHAT
                                  ? "微信联系人" : device_id->valuestring));
        const cJSON *app = cJSON_GetObjectItemCaseSensitive(item, "wx_app_id");
        const cJSON *model = cJSON_GetObjectItemCaseSensitive(item, "wx_model_id");
        if (cJSON_IsString(app) && app->valuestring != NULL) {
            (void)snprintf(contact->wx_app_id, sizeof(contact->wx_app_id),
                           "%s", app->valuestring);
        }
        if (cJSON_IsString(model) && model->valuestring != NULL) {
            (void)snprintf(contact->wx_model_id, sizeof(contact->wx_model_id),
                           "%s", model->valuestring);
        }
        if (contact->source == STARTER_CONTACT_WECHAT) {
            ++wechat_count;
            if (first_wechat.id[0] == '\0') first_wechat = value;
        }
        if (count < STARTER_PRODUCT_CONTACTS_MAX) parsed[count++] = value;
    }
    /* Combined contacts place device rows first. Preserve the first WeChat row
     * even when eight device contacts filled the bounded UI snapshot. */
    if (wechat_count > 0U) {
        bool present = false;
        for (uint8_t i = 0; i < count; ++i) {
            if (parsed[i].source == STARTER_CONTACT_WECHAT) { present = true; break; }
        }
        if (!present) parsed[STARTER_PRODUCT_CONTACTS_MAX - 1U] = first_wechat;
    }
    if (s_product_mutex == NULL ||
        xSemaphoreTake(s_product_mutex, pdMS_TO_TICKS(100)) != pdTRUE) {
        cJSON_Delete(root);
        return;
    }
    memcpy(s_product_snapshot.contacts, parsed, sizeof(parsed));
    s_product_snapshot.contact_count = count;
    s_product_snapshot.wechat_contacts_checked = true;
    s_product_snapshot.wechat_contact_count = wechat_count;
    xSemaphoreGive(s_product_mutex);
    cJSON_Delete(root);

    if (s_wechat_quick_pending) {
        s_wechat_quick_pending = false;
        if (wechat_count == 0U) return;
        uint8_t index = 0;
        for (; index < count; ++index) {
            if (parsed[index].source == STARTER_CONTACT_WECHAT) break;
        }
        if (index < count) dial_contact(index, false);
    }
}

static bool preempt_for_call(bool incoming)
{
    starter_runtime_state_t state = session_state();
    if (s_room_page_active && state != STARTER_RUNTIME_ROOM_CONNECTING &&
        state != STARTER_RUNTIME_ROOM_ACTIVE) room_set_foreground(false);
    /* A locally initiated call, or an incoming call after explicit answer,
     * becomes the foreground owner and may replace AI, Room or H5. Merely
     * receiving an incoming notification never calls this function. */
    if (state == STARTER_RUNTIME_AI_CONNECTING ||
        state == STARTER_RUNTIME_AI_ACTIVE ||
        state == STARTER_RUNTIME_H5_ACTIVE ||
        state == STARTER_RUNTIME_ROOM_CONNECTING ||
        state == STARTER_RUNTIME_ROOM_ACTIVE) {
        ESP_LOGI(TAG, "call preempts foreground owner=%s",
                 starter_runtime_state_name(state));
        diagnostic_event(incoming ? "incoming preempts" : "outgoing preempts", state);
        if (state == STARTER_RUNTIME_ROOM_CONNECTING ||
            state == STARTER_RUNTIME_ROOM_ACTIVE)
            room_set_foreground(false);
        else
            finish_session(0);
        state = session_state();
    }
    return state == STARTER_RUNTIME_WAITING;
}

static bool begin_call_common(const starter_product_contact_t *contact)
{
    if (contact == NULL ||
        !platform_client_ready() || !starter_tirtc_started()) return false;
    if (contact->source != STARTER_CONTACT_WECHAT && s_call_cleanup_room[0] != '\0') {
        product_set_call_result("正在结束上次通话，请等待");
        return false;
    }
    if (contact->source == STARTER_CONTACT_WECHAT && !s_voip_profile_ready) {
        ESP_LOGW(TAG, "WeChat call blocked until VoIP profile is reported");
        return false;
    }
    if (!preempt_for_call(false)) return false;
    starter_tirtc_accept_h5(false);
    starter_media_stop();
    if (starter_tirtc_connected()) {
        (void)starter_tirtc_disconnect();
    }
    s_connection_generation = 0;
    s_call_wechat = contact->source == STARTER_CONTACT_WECHAT;
    xiaotai_session_owner_t owner = s_call_wechat
        ? XIAOTAI_OWNER_WECHAT_VOIP : XIAOTAI_OWNER_DEVICE_CALL;
    if (!xiaotai_runtime_begin(&s_session, owner, false)) return false;
    s_call_outgoing = true;
    (void)snprintf(s_call_peer_id, sizeof(s_call_peer_id), "%s", contact->id);
    (void)snprintf(s_call_peer_name, sizeof(s_call_peer_name), "%s", contact->name);
    (void)snprintf(s_call_wx_app_id, sizeof(s_call_wx_app_id), "%s", contact->wx_app_id);
    (void)snprintf(s_call_wx_model_id, sizeof(s_call_wx_model_id), "%s", contact->wx_model_id);
    arm_session_timeout(CALL_CONNECT_TIMEOUT_MS);
    product_snapshot_reset();
    product_set_call(false, s_call_wechat, s_call_peer_name, false);
    publish_state();
    diagnostic_event("outgoing call", s_call_wechat);
    return true;
}

static void dial_contact(uint8_t index, bool video)
{
#if !CONFIG_IDF_TARGET_ESP32P4
    video = false;
#endif
    starter_product_contact_t contact = {0};
    if (s_product_mutex == NULL ||
        xSemaphoreTake(s_product_mutex, pdMS_TO_TICKS(100)) != pdTRUE) {
        return;
    }
    bool valid = index < s_product_snapshot.contact_count;
    if (valid) {
        contact = s_product_snapshot.contacts[index];
    }
    xSemaphoreGive(s_product_mutex);
    if (!valid || !begin_call_common(&contact)) {
        ESP_LOGW(TAG, "[DEBUG-call] dial rejected index=%u valid=%d", (unsigned)index,
                 valid ? 1 : 0);
        return;
    }
#if CONFIG_IDF_TARGET_ESP32P4
    starter_media_set_call_video(video);
    s_call_video = video;
    s_call_camera_enabled = video;
    if (video) configure_remote_video_presentation(NULL, s_call_wechat);
    product_set_call(false, s_call_wechat, s_call_peer_name, false);
#endif

    char body[512];
    platform_service_t service;
    const char *path;
    call_http_stage_t stage;
    if (s_call_wechat) {
        (void)snprintf(body, sizeof(body),
                       "{\"device_id\":\"%s\",\"wx_app_id\":\"%s\","
                       "\"wx_user_openid\":\"%s\",\"wx_model_id\":\"%s\","
                       "\"wx_room_type\":\"%s\",\"wx_version_type\":0}",
                       s_device_id, s_call_wx_app_id, s_call_peer_id,
                       s_call_wx_model_id, video ? "video" : "voice");
        service = PLATFORM_SERVICE_VOIP;
        path = "/v1/voip/device/call";
        stage = CALL_HTTP_VOIP_DIAL;
    } else {
        /* 主叫的 P2P 是入站 SDK 连接；先设门禁，避免 HTTP 响应与接听竞态。 */
        starter_tirtc_expect_call(session_generation());
        (void)snprintf(body, sizeof(body),
                       "{\"targets\":[\"%s\"],\"call_type\":\"%s\","
                       "\"camera_rotation\":270}",
                       s_call_peer_id, video ? "video" : "audio");
        service = PLATFORM_SERVICE_CALL;
        path = "/v1/call/request";
        stage = CALL_HTTP_DEVICE_DIAL;
    }
    esp_err_t err = platform_client_request_timeout(
        service, path, body, CALL_CONNECT_TIMEOUT_MS - 2000U,
        call_http_response, call_http_tag(session_generation(), stage));
    ESP_LOGI(TAG, "[DEBUG-call] dial submitted target=%s type=%s stage=%d ret=%s",
             s_call_peer_id, s_call_wechat ? "wechat" : "device", (int)stage,
             esp_err_to_name(err));
    if (err != ESP_OK) {
        finish_call_session(err, "网络中断");
    }
}

static void connect_voip(void);

static void accept_call(void)
{
    if (!session_incoming_pending() || !preempt_for_call(true)) {
        return;
    }
    xiaotai_session_owner_t accepted = XIAOTAI_OWNER_NONE;
    uint32_t generation = 0U;
    if (!xiaotai_runtime_accept_incoming(&s_session, &accepted, &generation)) {
        return;
    }
    starter_tirtc_accept_h5(false);
#if CONFIG_IDF_TARGET_ESP32P4
    starter_media_set_call_video(s_call_video);
#endif
    product_set_call(false, s_call_wechat, s_call_peer_name, false);
    publish_state();
    arm_session_timeout(CALL_CONNECT_TIMEOUT_MS);
    if (s_call_wechat) {
        connect_voip();
        return;
    }
    char body[384];
    (void)snprintf(body, sizeof(body),
                   "{\"device_id\":\"%s\",\"room_id\":\"%s\","
                   "\"purpose\":\"call\",\"camera_rotation\":270}",
                   s_call_peer_id, s_call_room_id);
    esp_err_t err = platform_client_request_timeout(
        PLATFORM_SERVICE_CALL, "/v1/call/device/info", body,
        CALL_CONNECT_TIMEOUT_MS - 2000U, call_http_response,
        call_http_tag(generation, CALL_HTTP_DEVICE_INFO));
    if (err != ESP_OK) {
        finish_call_session(err, "网络中断");
    }
}

/*
 * MQTT call_incoming 的解析帧里有多组 1 KiB peer/token 临时缓冲。不能在
 * 该深栈函数中再进入 TiRtcWhipConnect，否则 SDK 的握手栈会压穿
 * starter_session。下一轮事件在解析帧已退出后才提交 WHIP。
 */
static void voip_connect_task(void *argument)
{
    voip_connect_request_t *request = argument;
    runtime_event_t event = {
        .type = EVENT_VOIP_CONNECT_RESULT,
        .generation = request == NULL ? 0U : request->generation,
        .error = ESP_ERR_INVALID_ARG,
    };
    if (request != NULL) {
        event.error = starter_tirtc_voip_connect(request->peer_id,
                                                 request->token,
                                                 request->generation);
        ESP_LOGI(TAG, "[DEBUG-voip] WHIP worker completed generation=%lu rc=%d",
                 (unsigned long)request->generation, event.error);
        free(request);
    }
    if (!queue_event(&event)) {
        ESP_LOGE(TAG, "[DEBUG-voip] WHIP result queue full; session will time out");
    }
    /* WithCaps uses a statically registered stack/TCB. Ordinary deletion
     * removes the task but leaks both allocations on every WHIP attempt. */
    vTaskDeleteWithCaps(NULL);
}

static void connect_voip(void)
{
    starter_runtime_state_t state = session_state();
    if (!s_call_wechat || state != STARTER_RUNTIME_CALL_CONNECTING ||
        s_call_connect_peer[0] == '\0' || s_call_connect_token[0] == '\0') {
        return;
    }
    if (s_voip_connect_inflight) {
        return;
    }
    /* 先完成小对象分配，不能在释放 17 KiB 连续块后把它切碎。 */
    voip_connect_request_t *request = calloc(1, sizeof(*request));
    if (request == NULL) {
        finish_call_session(ESP_ERR_NO_MEM, "内存不足");
        return;
    }
    request->generation = session_generation();
    (void)snprintf(request->peer_id, sizeof(request->peer_id), "%s",
                   s_call_connect_peer);
    (void)snprintf(request->token, sizeof(request->token), "%s",
                   s_call_connect_token);
    if (!suspend_mqtt_for_external_connect()) {
        free(request);
        finish_call_session(ESP_ERR_NO_MEM, "内存不足");
        return;
    }
    s_voip_connect_inflight = true;
    if (xTaskCreateWithCaps(voip_connect_task, "voip_whip",
                            VOIP_CONNECT_TASK_STACK_BYTES, request, 5, NULL,
                            MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT) != pdPASS) {
        s_voip_connect_inflight = false;
        free(request);
        resume_mqtt_after_external_connect();
        finish_call_session(ESP_ERR_NO_MEM, "内存不足");
        return;
    }
    ESP_LOGI(TAG, "[DEBUG-voip] WHIP worker started outgoing=%d generation=%lu",
             s_call_outgoing ? 1 : 0, (unsigned long)session_generation());
    arm_session_timeout(CALL_CONNECT_TIMEOUT_MS);
}

static void handle_voip_connect_result(const runtime_event_t *event)
{
    starter_runtime_state_t state = session_state();
    if (event->generation != session_generation() || !s_call_wechat ||
        state != STARTER_RUNTIME_CALL_CONNECTING) {
        return;
    }
    s_voip_connect_inflight = false;
    if (event->error != 0) {
        resume_mqtt_after_external_connect();
        finish_call_session(event->error, "网络中断");
        return;
    }
    /* 成功只说明 WHIP 已提交；真正媒体启动由 0x2000 处理。 */
    arm_session_timeout(CALL_CONNECT_TIMEOUT_MS);
}

static void reject_wechat_values(const char *app_id,
                                 const char *model_id,
                                 const char *session_token,
                                 const char *room_id,
                                 const char *payload,
                                 int reason)
{
    if (app_id == NULL || app_id[0] == '\0' || model_id == NULL ||
        model_id[0] == '\0' || room_id == NULL || room_id[0] == '\0') {
        return;
    }
    cJSON *root = cJSON_CreateObject();
    bool ok = root != NULL &&
              cJSON_AddStringToObject(root, "wx_app_id", app_id) &&
              cJSON_AddStringToObject(root, "wx_model_id", model_id) &&
              cJSON_AddStringToObject(root, "wx_session_token",
                                     session_token == NULL ? "" : session_token) &&
              cJSON_AddStringToObject(root, "wx_room_id", room_id) &&
              cJSON_AddStringToObject(root, "wx_payload",
                                     payload == NULL ? "" : payload) &&
              cJSON_AddNumberToObject(root, "hangup_reason", reason);
    char *body = ok ? cJSON_PrintUnformatted(root) : NULL;
    cJSON_Delete(root);
    if (body != NULL) {
        (void)starter_tirtc_service_request("/v1/wxvoip/reject", body);
        cJSON_free(body);
    }
}

static void reject_or_hangup_call(bool reject)
{
    starter_runtime_state_t state = session_state();
    if (session_incoming_pending()) {
        if (s_call_wechat) {
            reject_wechat_values(s_call_wx_app_id,
                                 s_call_wx_model_id,
                                 s_call_wx_session_token,
                                 s_call_room_id,
                                 s_call_wx_payload,
                                 7);
        } else if (s_call_room_id[0] != '\0') {
            char body[224];
            (void)snprintf(body, sizeof(body),
                           "{\"room_id\":\"%s\",\"reason\":\"decline\"}",
                           s_call_room_id);
            (void)platform_client_request(PLATFORM_SERVICE_CALL,
                                          "/v1/call/reject", body, NULL, NULL);
        }
        (void)xiaotai_runtime_cancel_incoming(&s_session, NULL);
        s_call_room_id[0] = '\0';
        s_call_peer_id[0] = '\0';
        s_call_peer_name[0] = '\0';
        s_call_wx_app_id[0] = '\0';
        s_call_wx_model_id[0] = '\0';
        s_call_connect_peer[0] = '\0';
        s_call_connect_token[0] = '\0';
        s_call_wx_session_token[0] = '\0';
        s_call_wx_payload[0] = '\0';
        s_call_id[0] = '\0';
        s_call_wechat = false;
        s_call_outgoing = false;
#if CONFIG_IDF_TARGET_ESP32P4
        s_call_video = false;
        s_call_camera_enabled = false;
#endif
        product_set_call(false, false, "", false);
        diagnostic_event(reject ? "incoming rejected" : "incoming cleared", 0);
        return;
    }
    if (state != STARTER_RUNTIME_CALL_INCOMING &&
        state != STARTER_RUNTIME_CALL_CONNECTING &&
        state != STARTER_RUNTIME_CALL_ACTIVE) {
        return;
    }
    if (starter_tirtc_connected()) {
        /* A non-empty reason is part of the peer-call contract.  Keep the
         * transport alive briefly so the reliable data-channel command can
         * leave the SDK before local teardown closes the connection. */
        static const char hangup_command[] = "{\"reason\":0}";
        (void)starter_tirtc_send_command(CALL_COMMAND_HANGUP,
                                         hangup_command,
                                         sizeof(hangup_command) - 1U);
        vTaskDelay(pdMS_TO_TICKS(CALL_HANGUP_FLUSH_MS));
    }
    char body[1200];
    if (s_call_wechat) {
        if (reject && !s_call_outgoing) {
            reject_wechat_values(s_call_wx_app_id,
                                 s_call_wx_model_id,
                                 s_call_wx_session_token,
                                 s_call_room_id,
                                 s_call_wx_payload,
                                 7);
        }
    } else if (s_call_room_id[0] != '\0') {
        const char *path = state == STARTER_RUNTIME_CALL_INCOMING
                               ? "/v1/call/reject"
                               : (state == STARTER_RUNTIME_CALL_CONNECTING && s_call_outgoing
                                      ? "/v1/call/cancel" : "/v1/call/hangup");
        (void)snprintf(body, sizeof(body),
                       strcmp(path, "/v1/call/cancel") == 0
                           ? "{\"room_id\":\"%s\"}"
                           : "{\"room_id\":\"%s\",\"reason\":\"%s\"}",
                       s_call_room_id,
                       reject ? "decline" : "hangup");
        (void)platform_client_request(PLATFORM_SERVICE_CALL, path, body, NULL, NULL);
    }
    finish_session(0);
}

static void begin_ai_session(uint32_t wake_token)
{
    /*
     * AI 优先于 H5：先关闭 H5 入站门禁并结束当前媒体/连接，再开启新会话代次。
     * HTTP 响应携带该代次，迟到响应不能推动后续新会话。
     */
    starter_runtime_state_t state = session_state();
    bool platform_ready = platform_client_ready();
    bool tirtc_ready = starter_tirtc_started();
    bool microphone_muted = starter_media_status().microphone_muted;
    if ((state != STARTER_RUNTIME_WAITING &&
         state != STARTER_RUNTIME_H5_ACTIVE &&
         state != STARTER_RUNTIME_ROOM_CONNECTING &&
         state != STARTER_RUNTIME_ROOM_ACTIVE) || microphone_muted) {
        ESP_LOGW(TAG,
                 "AI start rejected: state=%s platform_ready=%d tirtc_ready=%d microphone_muted=%d",
                 starter_runtime_state_name(state), platform_ready ? 1 : 0,
                 tirtc_ready ? 1 : 0, microphone_muted ? 1 : 0);
        starter_media_cancel_ai_preroll(wake_token);
        s_ai_start_pending = false;
        s_ai_ready_deadline_ms = 0;
        product_set_ai_start_pending(false, "现在暂时不能开始对话");
        return;
    }
    if (!platform_ready || !tirtc_ready) {
        ESP_LOGI(TAG,
                 "AI start waiting: platform_ready=%d tirtc_ready=%d microphone_muted=%d",
                 platform_ready ? 1 : 0, tirtc_ready ? 1 : 0,
                 microphone_muted ? 1 : 0);
        if (wake_token != 0U) {
            /* Acoustic preroll is time-bounded; never replay stale speech. */
            starter_media_cancel_ai_preroll(wake_token);
            product_set_ai_start_pending(false, "AI 服务启动中，请稍候…");
            return;
        }
        if (!s_ai_start_pending) {
            s_ai_ready_deadline_ms = now_ms() + AI_READY_WAIT_TIMEOUT_MS;
        }
        s_ai_start_pending = true;
        product_set_ai_start_pending(true, "AI 服务启动中，请稍候…");
        return;
    }

    s_ai_start_pending = false;
    s_ai_ready_deadline_ms = 0;

    if (s_room_page_active || state == STARTER_RUNTIME_ROOM_CONNECTING ||
        state == STARTER_RUNTIME_ROOM_ACTIVE) {
        room_set_foreground(false);
    }
    starter_tirtc_accept_h5(false);
    starter_media_set_wake_allowed(false);
    if (starter_media_stop_for_ai(wake_token) != ESP_OK) {
        ESP_LOGW(TAG, "AI start ignored: wake audio expired or cancelled");
        finish_session(ESP_ERR_INVALID_STATE);
        return;
    }
    if (starter_tirtc_connected()) {
        (void)starter_tirtc_disconnect();
    }
    s_connection_generation = 0;
    if (!xiaotai_runtime_begin(&s_session, XIAOTAI_OWNER_AI, false)) {
        starter_media_cancel_ai_preroll(wake_token);
        return;
    }
    arm_session_timeout(AI_REQUEST_TIMEOUT_MS);
    atomic_store_explicit(&s_last_error, 0, memory_order_release);
    product_snapshot_reset();
    product_set_phase(STARTER_AI_UI_LISTENING);
    publish_state();

    /* MQTT bearer 由 platform_client 内部添加，状态机不接触设备密钥。 */
    s_diagnostic_ai_started_ms = now_ms();
    diagnostic_event(wake_token != 0 ? "wake accepted" : "manual AI start", 0);
    if (wake_token != 0) {
        ESP_LOGI(TAG, "AI wake request accepted token=%lu session=%lu",
                 (unsigned long)wake_token, (unsigned long)session_generation());
    }
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

static void retry_pending_ai_start(int64_t current_ms)
{
    if (!s_ai_start_pending) return;
    starter_runtime_state_t state = session_state();
    if (state != STARTER_RUNTIME_WAITING && state != STARTER_RUNTIME_H5_ACTIVE &&
        state != STARTER_RUNTIME_ROOM_CONNECTING &&
        state != STARTER_RUNTIME_ROOM_ACTIVE) {
        s_ai_start_pending = false;
        s_ai_ready_deadline_ms = 0;
        product_set_ai_start_pending(false, "现在暂时不能开始对话");
        return;
    }
    if (current_ms >= s_ai_ready_deadline_ms) {
        ESP_LOGW(TAG, "AI readiness wait timed out");
        s_ai_start_pending = false;
        s_ai_ready_deadline_ms = 0;
        product_set_ai_start_pending(false, "AI 服务暂不可用，请稍后重试");
        return;
    }
    if (platform_client_ready() && starter_tirtc_started()) {
        ESP_LOGI(TAG, "AI readiness reached; starting pending manual request");
        s_ai_start_pending = false;
        begin_ai_session(0U);
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
    ai_credentials_t *credentials = calloc(1, sizeof(*credentials));
    if (credentials == NULL) {
        cJSON_Delete(root);
        finish_session(ESP_ERR_NO_MEM);
        return;
    }
    bool ok = cJSON_IsNumber(code) &&
              (code->valueint == 0 || code->valueint == 200) &&
              cJSON_IsObject(data) &&
              copy_json_string(data,
                               "peer_id",
                               credentials->peer_id,
                               sizeof(credentials->peer_id),
                               true) &&
              copy_json_string(data,
                               "token",
                               credentials->token,
                               sizeof(credentials->token),
                               true) &&
              copy_json_string(data,
                               "role_id",
                               s_ai_role_id,
                               sizeof(s_ai_role_id),
                               false);
    cJSON_Delete(root);
    if (!ok) {
        free(credentials);
        ESP_LOGE(TAG, "AI token response is invalid");
        finish_session(ESP_ERR_INVALID_RESPONSE);
        return;
    }
    /*
     * MQTT/TLS 的常驻分配会切碎内部堆；WHIP 提交前短暂停止 MQTT，给 SDK
     * 的 16+ KiB 连续分配让路，连接回调到达后立即恢复。
     */
    if (!suspend_mqtt_for_external_connect()) {
        free(credentials);
        finish_session(ESP_ERR_NO_MEM);
        return;
    }
    int rc = starter_tirtc_ai_connect(credentials->peer_id,
                                      credentials->token,
                                      session_generation());
    free(credentials);
    if (rc != 0) {
        resume_mqtt_after_external_connect();
        ESP_LOGE(TAG, "AI connection submission failed rc=%d", rc);
        finish_session(rc);
        return;
    }
    arm_session_timeout(AI_CONNECT_TIMEOUT_MS);
}

static void handle_call_http(const runtime_event_t *event)
{
    if (event->request_tag != session_generation() ||
        session_state() != STARTER_RUNTIME_CALL_CONNECTING) {
        return;
    }
    cJSON *root = event->text == NULL ? NULL :
        cJSON_ParseWithLength(event->text, event->length);
    const cJSON *code = cJSON_IsObject(root)
                            ? cJSON_GetObjectItemCaseSensitive(root, "code")
                            : NULL;
    const cJSON *message = cJSON_IsObject(root)
                               ? cJSON_GetObjectItemCaseSensitive(root, "message")
                               : NULL;
    if (!cJSON_IsString(message)) message = cJSON_GetObjectItemCaseSensitive(root, "msg");
    ESP_LOGI(TAG,
             "[DEBUG-call] HTTP result stage=%lu bytes=%u parse=%d code=%d message=%s",
             (unsigned long)event->command,
             (unsigned)event->length,
             root != NULL,
             cJSON_IsNumber(code) ? code->valueint : -1,
             cJSON_IsString(message) && message->valuestring != NULL
                 ? message->valuestring : "");
    if (event->command == CALL_HTTP_DEVICE_DIAL &&
        cJSON_IsNumber(code) && code->valueint == 40202) {
        char existing_room[129] = "";
        const cJSON *busy_data = cJSON_GetObjectItemCaseSensitive(root, "data");
        if (copy_json_string(busy_data, "room_id", existing_room,
                             sizeof(existing_room), true)) {
            schedule_call_cleanup(existing_room);
        }
        cJSON_Delete(root);
        finish_call_session(ESP_ERR_INVALID_STATE, "正在结束上次通话，请等待");
        return;
    }
    const cJSON *data = response_ok(root)
                            ? cJSON_GetObjectItemCaseSensitive(root, "data")
                            : NULL;
    if (!cJSON_IsObject(data)) {
        cJSON_Delete(root);
        finish_call_session(ESP_ERR_INVALID_RESPONSE, "呼叫失败");
        return;
    }
    if (event->command == CALL_HTTP_DEVICE_DIAL) {
        bool ok = copy_json_string(data, "room_id", s_call_room_id,
                                   sizeof(s_call_room_id), true);
        cJSON_Delete(root);
        if (!ok) {
            finish_call_session(ESP_ERR_INVALID_RESPONSE, "呼叫失败");
            return;
        }
        starter_tirtc_expect_call(session_generation());
        arm_session_timeout(CALL_CONNECT_TIMEOUT_MS);
        ESP_LOGI(TAG, "[DEBUG-call] room created room=%s; awaiting callee/p2p",
                 s_call_room_id);
        return;
    }
    if (event->command == CALL_HTTP_VOIP_DIAL) {
        (void)copy_json_string(data, "call_id", s_call_id,
                               sizeof(s_call_id), false);
        cJSON_Delete(root);
        /* 真正 peer/token/room 来自匹配的 MQTT call_incoming。 */
        arm_session_timeout(CALL_CONNECT_TIMEOUT_MS);
        return;
    }
    if (event->command == CALL_HTTP_DEVICE_INFO) {
        char token[1024];
        char remote_id[65];
        bool ok = copy_json_string(data, "token", token, sizeof(token), true) &&
                  copy_json_string(data, "device_id", remote_id,
                                   sizeof(remote_id), true);
#if CONFIG_IDF_TARGET_ESP32P4
        if (s_call_video) configure_remote_video_presentation(data, false);
#endif
        cJSON_Delete(root);
        if (!ok) {
            finish_call_session(ESP_ERR_INVALID_RESPONSE, "呼叫失败");
            return;
        }
        if (!suspend_mqtt_for_external_connect()) {
            finish_call_session(ESP_ERR_NO_MEM, "内存不足");
            return;
        }
        int rc = starter_tirtc_call_connect(remote_id, token,
                                            session_generation());
        ESP_LOGI(TAG, "[DEBUG-call] callee p2p submitted room=%s peer=%s rc=%d",
                 s_call_room_id, remote_id, rc);
        if (rc != 0) {
            resume_mqtt_after_external_connect();
            finish_call_session(rc, "网络中断");
        }
        return;
    }
    cJSON_Delete(root);
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
              cJSON_AddStringToObject(input, "codec", "opus") &&
              cJSON_AddNumberToObject(input, "sample_rate", 16000) &&
              cJSON_AddNumberToObject(input, "channels", 1) &&
              cJSON_AddStringToObject(output, "codec", "opus") &&
              cJSON_AddNumberToObject(output, "sample_rate", 16000) &&
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

static void send_room_join(void)
{
    s_room_start_at_ms = 0;
    starter_runtime_state_t state = session_state();
    if (state != STARTER_RUNTIME_ROOM_CONNECTING ||
        !starter_tirtc_connected() || s_connection_generation == 0U) {
        room_stop_connection("connect_failed", ESP_ERR_INVALID_STATE);
        return;
    }
    char join[512];
    int length = snprintf(join, sizeof(join),
        "{\"jsonrpc\":\"2.0\",\"id\":1,\"method\":\"join_room\","
        "\"params\":{\"room_id\":\"%s\",\"device_id\":\"%s\","
        "\"input_audio\":{\"codec\":\"g711a\",\"sample_rate\":8000,\"channels\":1},"
        "\"output_audio\":{\"codec\":\"g711a\",\"sample_rate\":8000,\"channels\":1}}}",
        s_room_id, s_device_id);
    if (length <= 0 || (size_t)length >= sizeof(join) ||
        starter_tirtc_send_command(ROOM_COMMAND, join, (uint32_t)length) < 0) {
        room_stop_connection("connect_failed", ESP_FAIL);
        return;
    }
    arm_session_timeout(AI_RESPONSE_TIMEOUT_MS);
    ESP_LOGI(TAG, "room join_room sent after data-channel settle");
}

static bool ai_audio_profile_valid(const cJSON *profile)
{
    const cJSON *codec = cJSON_IsObject(profile)
                             ? cJSON_GetObjectItemCaseSensitive(profile, "codec")
                             : NULL;
    const cJSON *rate = cJSON_IsObject(profile)
                            ? cJSON_GetObjectItemCaseSensitive(profile, "sample_rate")
                            : NULL;
    const cJSON *channels = cJSON_IsObject(profile)
                                ? cJSON_GetObjectItemCaseSensitive(profile, "channels")
                                : NULL;
    bool codec_ok = cJSON_IsString(codec) && codec->valuestring != NULL &&
                    strcmp(codec->valuestring, "opus") == 0;
    return codec_ok && cJSON_IsNumber(rate) && rate->valueint == 16000 &&
           cJSON_IsNumber(channels) && channels->valueint == 1;
}

static void maybe_activate_outgoing_device_call(const char *source)
{
    starter_runtime_state_t state = session_state();
    if (s_call_wechat || !s_call_outgoing ||
        state != STARTER_RUNTIME_CALL_CONNECTING ||
        !s_call_peer_answered || !s_call_p2p_connected ||
        s_connection_generation == 0U) {
        return;
    }
    if (starter_media_start(STARTER_TIRTC_CALL,
                            s_connection_generation) != ESP_OK) {
        ESP_LOGE(TAG, "[DEBUG-call] caller media start failed source=%s room=%s",
                 source, s_call_room_id);
        finish_call_session(ESP_ERR_INVALID_STATE, "音频启动失败");
        return;
    }
    s_call_waiting_confirm = false;
    (void)xiaotai_runtime_media_started(&s_session, session_generation());
    ESP_LOGI(TAG, "[DEBUG-call] caller active source=%s room=%s generation=%lu",
             source, s_call_room_id, (unsigned long)s_connection_generation);
    publish_state();
}

static void handle_connection(const runtime_event_t *event)
{
    /*
     * 连接回调可能在 disconnect 后迟到。先按 request_tag/generation 过滤，
     * 再根据当前业务状态决定接纳或关闭，避免旧连接复活。
     */
    starter_runtime_state_t state = session_state();
    /* Validate ownership BEFORE touching MQTT reserves or the foreground owner.
     * SDK callbacks may already be queued when finish_session cancels a handle. */
    bool ai = state == STARTER_RUNTIME_AI_CONNECTING || state == STARTER_RUNTIME_AI_ACTIVE;
    bool call = state == STARTER_RUNTIME_CALL_CONNECTING || state == STARTER_RUNTIME_CALL_ACTIVE;
    bool room = state == STARTER_RUNTIME_ROOM_CONNECTING || state == STARTER_RUNTIME_ROOM_ACTIVE;
    bool current = false;
    if (event->mode == STARTER_TIRTC_H5) {
        current = event->generation != 0 &&
            (event->flag ? state == STARTER_RUNTIME_WAITING :
             (state == STARTER_RUNTIME_H5_ACTIVE && event->generation == s_connection_generation));
    } else if ((ai && event->mode == STARTER_TIRTC_AI) ||
               (room && event->mode == STARTER_TIRTC_ROOM) ||
               (call && event->mode == (s_call_wechat ? STARTER_TIRTC_VOIP : STARTER_TIRTC_CALL))) {
        if (event->flag) {
            current = event->request_tag == session_generation() &&
                      event->generation != 0 && s_connection_generation == 0 &&
                      (state == STARTER_RUNTIME_AI_CONNECTING ||
                       state == STARTER_RUNTIME_CALL_CONNECTING ||
                       state == STARTER_RUNTIME_ROOM_CONNECTING);
        } else if (event->generation != 0) {
            current = event->generation == s_connection_generation;
        } else {
            current = event->request_tag != 0 && event->request_tag == session_generation() &&
                      s_connection_generation == 0;
        }
    }
    if (!current) {
        ESP_LOGI(TAG, "ignored stale connection event mode=%d generation=%lu request=%lu",
                 (int)event->mode, (unsigned long)event->generation,
                 (unsigned long)event->request_tag);
        return;
    }
    if (event->mode == STARTER_TIRTC_AI ||
        event->mode == STARTER_TIRTC_CALL ||
        event->mode == STARTER_TIRTC_VOIP ||
        event->mode == STARTER_TIRTC_ROOM) {
        resume_mqtt_after_external_connect();
    }
    if (!event->flag) {
        /* AI 意图转呼叫时 finish_session() 会主动断开旧 AI 连接。该断开
         * 回调可能晚于新呼叫的 HTTP 房间创建；它属于旧媒体所有者，绝不能
         * 终止正在等待被叫接入的 CALL/VOIP 会话。 */
        if ((state == STARTER_RUNTIME_CALL_INCOMING ||
             state == STARTER_RUNTIME_CALL_CONNECTING ||
             state == STARTER_RUNTIME_CALL_ACTIVE) &&
            event->mode != STARTER_TIRTC_CALL &&
            event->mode != STARTER_TIRTC_VOIP) {
            ESP_LOGI(TAG,
                     "ignore stale connection close mode=%d while call state=%s",
                     (int)event->mode, starter_runtime_state_name(state));
            return;
        }
        if (event->mode == STARTER_TIRTC_AI && event->request_tag != 0U &&
            event->request_tag != session_generation()) {
            return;
        }
        if (event->generation != 0U && s_connection_generation != 0U &&
            event->generation != s_connection_generation) {
            return;
        }
        if (event->mode == STARTER_TIRTC_AI &&
            state == STARTER_RUNTIME_AI_ACTIVE) {
            bool expected = xiaotai_ai_end_drain_accepts_remote_close(
                                &s_ai_end_drain, session_generation()) ||
                            ((event->error == 0 ||
                              event->error == STARTER_TIRTC_ERROR_REMOTE_CLOSE) &&
                             xiaotai_ai_remote_close_is_normal(
                                 &s_ai_end_drain,
                                 session_generation(),
                                 true));
            if (expected) {
                ESP_LOGI(TAG,
                         "AI transport closed normally; draining playback error=%d generation=%lu",
                         event->error,
                         (unsigned long)event->generation);
                if (!s_ai_end_drain.pending) {
                    begin_ai_end_drain(0U,
                                       AI_REMOTE_CLOSE_DRAIN_TIMEOUT_MS,
                                       true);
                } else {
                    s_ai_transport_closed = true;
                    starter_media_set_uplink_enabled(false);
                    log_ai_playback_status("transport-close");
                }
                service_ai_end_drain();
                return;
            }
        }
        ESP_LOGW(TAG, "connection ended mode=%d error=%d", (int)event->mode, event->error);
        if (state == STARTER_RUNTIME_ROOM_CONNECTING ||
            state == STARTER_RUNTIME_ROOM_ACTIVE) {
            room_stop_connection("connect_failed", event->error);
        } else if (state == STARTER_RUNTIME_CALL_INCOMING ||
            state == STARTER_RUNTIME_CALL_CONNECTING ||
            state == STARTER_RUNTIME_CALL_ACTIVE) {
            finish_call_session(event->error,
                                event->error != 0 ? "网络中断"
                                                  : "对方已挂断");
        } else {
            if (state == STARTER_RUNTIME_AI_CONNECTING ||
                state == STARTER_RUNTIME_AI_ACTIVE) {
                ESP_LOGI(TAG,
                         "AI session closed by transport error=%d generation=%lu",
                         event->error, (unsigned long)event->generation);
            }
            finish_session(event->error);
        }
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
    if (event->mode == STARTER_TIRTC_ROOM &&
        state == STARTER_RUNTIME_ROOM_CONNECTING &&
        event->request_tag == session_generation()) {
        s_connection_generation = event->generation;
        s_room_start_at_ms = now_ms() + ROOM_START_SETTLE_MS;
        arm_session_timeout(ROOM_CONNECT_TIMEOUT_MS);
        publish_state();
        return;
    }
    if ((event->mode == STARTER_TIRTC_CALL ||
         event->mode == STARTER_TIRTC_VOIP) &&
        state == STARTER_RUNTIME_CALL_CONNECTING &&
        event->request_tag == session_generation()) {
        s_connection_generation = event->generation;
        publish_state();
        /*
         * WHIP 成功仅表示传输连接建立。微信 VoIP 与参考工程一致，必须
         * 再等待服务端的 0x2000 / CALL_CONNECTED，收到后才允许启动 A-law
         * 媒体；过早发音频会被服务端关闭，表现为无声后自动回首页。
         */
        if (event->mode == STARTER_TIRTC_VOIP) {
            s_call_waiting_confirm = true;
            arm_session_timeout(VOIP_CONNECTED_WAIT_TIMEOUT_MS);
            ESP_LOGI(TAG, "VoIP WHIP connected; awaiting CALL_CONNECTED generation=%lu",
                     (unsigned long)s_connection_generation);
            return;
        }
        s_call_waiting_confirm = true;
        if (event->mode == STARTER_TIRTC_CALL && !s_call_outgoing) {
            char confirm[192];
            int length = snprintf(confirm, sizeof(confirm),
                                  "{\"room_id\":\"%s\"}", s_call_room_id);
            if (length <= 0 || (size_t)length >= sizeof(confirm) ||
                starter_tirtc_send_command(CALL_COMMAND_CONNECT,
                                           confirm,
                                           (uint32_t)length) < 0 ||
                starter_media_start(STARTER_TIRTC_CALL,
                                    s_connection_generation) != ESP_OK) {
                finish_session(ESP_FAIL);
                return;
            }
            s_call_waiting_confirm = false;
            (void)xiaotai_runtime_media_started(&s_session, session_generation());
            publish_state();
        } else {
            arm_session_timeout(AI_RESPONSE_TIMEOUT_MS);
            if (event->mode == STARTER_TIRTC_CALL && s_call_outgoing) {
                s_call_p2p_connected = true;
                ESP_LOGI(TAG,
                         "[DEBUG-call] caller p2p accepted room=%s answered=%d",
                         s_call_room_id, s_call_peer_answered ? 1 : 0);
                maybe_activate_outgoing_device_call("p2p");
            }
        }
        return;
    }

    ESP_LOGW(TAG, "unexpected connection mode=%d; closing", (int)event->mode);
    finish_session(ESP_ERR_INVALID_STATE);
}

static void handle_call_command(const runtime_event_t *event)
{
    starter_runtime_state_t state = session_state();
    if ((event->mode != STARTER_TIRTC_CALL &&
         event->mode != STARTER_TIRTC_VOIP) ||
        event->generation != s_connection_generation) {
        return;
    }
    if (event->command == CALL_COMMAND_HANGUP) {
        finish_call_session(0, "对方已挂断");
        return;
    }
    if (event->command != CALL_COMMAND_CONNECT ||
        state != STARTER_RUNTIME_CALL_CONNECTING || !s_call_waiting_confirm) {
        return;
    }
    if (!s_call_wechat && event->length > 0U) {
        cJSON *root = cJSON_ParseWithLength(event->text, event->length);
        const cJSON *room = root == NULL ? NULL :
            cJSON_GetObjectItemCaseSensitive(root, "room_id");
        bool matches = cJSON_IsString(room) && room->valuestring != NULL &&
                       strcmp(room->valuestring, s_call_room_id) == 0;
        cJSON_Delete(root);
        if (!matches) {
            return;
        }
    }
    if (!s_call_wechat && s_call_outgoing) {
        /* 0x2000 保持兼容，但不再成为唯一的通话开通条件。 */
        s_call_peer_answered = true;
        maybe_activate_outgoing_device_call("command");
        return;
    }
    starter_tirtc_mode_t mode = s_call_wechat ? STARTER_TIRTC_VOIP
                                               : STARTER_TIRTC_CALL;
    int subscribe_rc = starter_tirtc_subscribe_call_audio();
    if (subscribe_rc < 0) {
        finish_call_session(subscribe_rc, "网络中断");
        return;
    }
    if (starter_media_start(mode, s_connection_generation) != ESP_OK) {
        finish_session(ESP_ERR_INVALID_STATE);
        return;
    }
    s_call_waiting_confirm = false;
    (void)xiaotai_runtime_media_started(&s_session, session_generation());
    publish_state();
}

typedef enum {
    CONTACT_MATCH_NOT_FOUND = 0,
    CONTACT_MATCH_UNIQUE,
    CONTACT_MATCH_AMBIGUOUS,
} contact_match_t;

static bool contact_channel_matches(const starter_product_contact_t *contact,
                                    const char *channel)
{
    if (channel == NULL || channel[0] == '\0') {
        return true;
    }
    if (strcmp(channel, "wx") == 0 || strcmp(channel, "wechat") == 0 ||
        strcmp(channel, "voip") == 0) {
        return contact->source == STARTER_CONTACT_WECHAT;
    }
    if (strcmp(channel, "device") == 0 || strcmp(channel, "call") == 0 ||
        strcmp(channel, "tirtc") == 0) {
        return contact->source == STARTER_CONTACT_DEVICE;
    }
    return false;
}

static bool normalize_contact_name(const char *input,
                                   char *output,
                                   size_t capacity)
{
    if (input == NULL || output == NULL || capacity == 0U) {
        return false;
    }
    size_t used = 0;
    for (const unsigned char *cursor = (const unsigned char *)input;
         *cursor != '\0'; ++cursor) {
        if (*cursor < 0x80U && isspace(*cursor)) {
            continue;
        }
        if (used + 1U >= capacity) {
            output[0] = '\0';
            return false;
        }
        output[used++] = *cursor < 0x80U
                             ? (char)tolower(*cursor) : (char)*cursor;
    }
    output[used] = '\0';
    return used > 0U;
}

/*
 * AI 只能提出呼叫意图。最终目标必须在设备当前通讯录里唯一命中；禁止把
 * 大模型返回的任意 ID 直接交给拨号接口。
 */
static contact_match_t find_contact(const char *target_id,
                                    const char *target_name,
                                    const char *channel,
                                    uint8_t *index)
{
    if (index == NULL || s_product_mutex == NULL ||
        xSemaphoreTake(s_product_mutex, pdMS_TO_TICKS(100)) != pdTRUE) {
        return CONTACT_MATCH_NOT_FOUND;
    }
    char normalized_target[65] = "";
    bool match_by_id = target_id != NULL && target_id[0] != '\0';
    bool target_valid = normalize_contact_name(target_name,
                                               normalized_target,
                                               sizeof(normalized_target));
    uint8_t matches = 0;
    bool id_consistent = !match_by_id;
    for (uint8_t i = 0; i < s_product_snapshot.contact_count; ++i) {
        const starter_product_contact_t *contact = &s_product_snapshot.contacts[i];
        if (!target_valid || !contact_channel_matches(contact, channel)) {
            continue;
        }
        char normalized_contact[65];
        bool matched = normalize_contact_name(contact->name,
                                             normalized_contact,
                                             sizeof(normalized_contact)) &&
                      strcmp(normalized_contact, normalized_target) == 0;
        if (matched) {
            *index = i;
            ++matches;
            if (match_by_id && strcmp(contact->id, target_id) == 0) {
                id_consistent = true;
            }
        }
    }
    xSemaphoreGive(s_product_mutex);
    return matches == 1U && id_consistent ? CONTACT_MATCH_UNIQUE
           : matches > 1U ? CONTACT_MATCH_AMBIGUOUS
                          : CONTACT_MATCH_NOT_FOUND;
}

/* AI 服务的 device_action 是一个可扩展的工具调用协议。不同角色版本会把
 * 参数放在 data/arguments 等包装对象中；在这里归一化，后面的拨号策略始终
 * 只面对本机通讯录中的联系人，绝不相信模型给出的任意设备 ID。 */
static const cJSON *action_payload(const cJSON *params)
{
    if (!cJSON_IsObject(params)) {
        return NULL;
    }
    static const char *const wrappers[] = {
        "data", "arguments", "args", "input", "payload", "parameters",
    };
    for (size_t i = 0; i < sizeof(wrappers) / sizeof(wrappers[0]); ++i) {
        const cJSON *nested = cJSON_GetObjectItemCaseSensitive(params, wrappers[i]);
        if (cJSON_IsObject(nested)) {
            return nested;
        }
    }
    return params;
}

static bool copy_first_json_string(const cJSON *object,
                                   const char *const *names,
                                   size_t count,
                                   char *destination,
                                   size_t capacity)
{
    if (destination == NULL || capacity == 0U) {
        return false;
    }
    destination[0] = '\0';
    if (!cJSON_IsObject(object)) {
        return false;
    }
    for (size_t i = 0; i < count; ++i) {
        const cJSON *item = cJSON_GetObjectItemCaseSensitive(object, names[i]);
        if (!cJSON_IsString(item) || item->valuestring == NULL ||
            item->valuestring[0] == '\0' || strlen(item->valuestring) >= capacity) {
            continue;
        }
        (void)snprintf(destination, capacity, "%s", item->valuestring);
        return true;
    }
    return false;
}

static bool ai_call_action_name(const char *action)
{
    return action != NULL &&
           (strcmp(action, "call") == 0 || strcmp(action, "call_contact") == 0 ||
            strcmp(action, "call_device") == 0 || strcmp(action, "device_call") == 0 ||
            strcmp(action, "start_device_call") == 0 ||
            strcmp(action, "call_wechat") == 0 || strcmp(action, "wechat_call") == 0 ||
            strcmp(action, "start_voip_call") == 0);
}

static void send_device_action_result(const cJSON *request_id,
                                      bool ok,
                                      const char *status,
                                      const char *message)
{
    /* JSON-RPC notification 没有 id，无需也不能回复。 */
    if (request_id == NULL || (!cJSON_IsString(request_id) && !cJSON_IsNumber(request_id))) {
        return;
    }
    cJSON *root = cJSON_CreateObject();
    cJSON *result = cJSON_CreateObject();
    bool valid = root != NULL && result != NULL &&
                 cJSON_AddStringToObject(root, "jsonrpc", "2.0") &&
                 cJSON_AddItemToObject(root, "id", cJSON_Duplicate(request_id, true)) &&
                 cJSON_AddBoolToObject(result, "ok", ok) &&
                 cJSON_AddStringToObject(result, "status", status) &&
                 cJSON_AddStringToObject(result, "message", message);
    if (valid) {
        cJSON_AddItemToObject(root, "result", result);
        result = NULL;
        char *json = cJSON_PrintUnformatted(root);
        if (json != NULL) {
            (void)starter_tirtc_send_command(AI_COMMAND, json, (uint32_t)strlen(json));
            cJSON_free(json);
        }
    }
    cJSON_Delete(result);
    cJSON_Delete(root);
}

static contact_match_t parse_ai_call_action(const cJSON *params,
                                            bool legacy_call_intent,
                                            char *target_id,
                                            size_t target_id_capacity,
                                            char *target_name,
                                            size_t target_name_capacity,
                                            char *channel,
                                            size_t channel_capacity,
                                            char *media,
                                            size_t media_capacity,
                                            uint8_t *contact_index)
{
    const cJSON *payload = action_payload(params);
    static const char *const action_names[] = {"action", "name", "tool", "function"};
    static const char *const target_id_names[] = {
        "target_id", "target_device_id", "device_id", "callee_device_id", "peer_id",
    };
    static const char *const target_name_names[] = {
        "target_name", "target", "contact_name", "contact", "device_name",
        "device_alias", "nickname", "alias", "remark", "callee", "name",
    };
    static const char *const channel_names[] = {"channel", "source", "type"};
    static const char *const media_names[] = {"media", "call_type", "room_type"};
    char action[32] = "";
    if (!legacy_call_intent &&
        (!(copy_first_json_string(params, action_names,
                                  sizeof(action_names) / sizeof(action_names[0]),
                                  action, sizeof(action)) ||
            copy_first_json_string(payload, action_names,
                                   sizeof(action_names) / sizeof(action_names[0]),
                                   action, sizeof(action))) ||
         !ai_call_action_name(action))) {
        return CONTACT_MATCH_NOT_FOUND;
    }
    (void)copy_first_json_string(payload, target_id_names,
                                 sizeof(target_id_names) / sizeof(target_id_names[0]),
                                 target_id, target_id_capacity);
    (void)copy_first_json_string(payload, target_name_names,
                                 sizeof(target_name_names) / sizeof(target_name_names[0]),
                                 target_name, target_name_capacity);
    (void)copy_first_json_string(payload, channel_names,
                                 sizeof(channel_names) / sizeof(channel_names[0]),
                                 channel, channel_capacity);
    (void)copy_first_json_string(payload, media_names,
                                 sizeof(media_names) / sizeof(media_names[0]),
                                 media, media_capacity);
    bool audio_only = media[0] == '\0' || strcmp(media, "audio") == 0 ||
                      strcmp(media, "voice") == 0;
    return audio_only ? find_contact(target_id, target_name, channel, contact_index)
                      : CONTACT_MATCH_NOT_FOUND;
}

static void handle_ai_command(const runtime_event_t *event)
{
    /* 当前 AI 连接上的 start_session 响应与 UI 通知都在状态任务串行处理。 */
    if (event->mode != STARTER_TIRTC_AI || event->command != AI_COMMAND ||
        event->generation != s_connection_generation) {
        diagnostic_event("AI stale command", (int)event->generation);
        return;
    }
    cJSON *root = event->text == NULL
                      ? NULL
                      : cJSON_ParseWithLength(event->text, event->length);
    if (root == NULL) {
        diagnostic_event("AI invalid JSON", 0);
        return;
    }
    const cJSON *id = root == NULL
                          ? NULL
                          : cJSON_GetObjectItemCaseSensitive(root, "id");
    /* result/error 都必须属于当前 start_session，不能让无 ID 的旁路命令改状态。 */
    bool id_matches = cJSON_IsString(id) && id->valuestring != NULL &&
                      strcmp(id->valuestring, s_ai_request_id) == 0;
    bool rejected = root != NULL && id_matches &&
                    cJSON_GetObjectItemCaseSensitive(root, "error") != NULL;
    const cJSON *result = cJSON_GetObjectItemCaseSensitive(root, "result");
    const cJSON *session_id = cJSON_IsObject(result)
                                  ? cJSON_GetObjectItemCaseSensitive(result, "session_id")
                                  : NULL;
    const cJSON *input_audio = cJSON_IsObject(result)
                                   ? cJSON_GetObjectItemCaseSensitive(result, "input_audio")
                                   : NULL;
    const cJSON *output_audio = cJSON_IsObject(result)
                                    ? cJSON_GetObjectItemCaseSensitive(result, "output_audio")
                                    : NULL;
    bool accepted = id_matches && cJSON_IsString(session_id) &&
                    session_id->valuestring != NULL && session_id->valuestring[0] != '\0' &&
                    ai_audio_profile_valid(input_audio) &&
                    ai_audio_profile_valid(output_audio);
    starter_runtime_state_t state = session_state();
    if (rejected) {
        ESP_LOGE(TAG, "AI start_session was rejected");
        cJSON_Delete(root);
        finish_session(ESP_FAIL);
    } else if (state == STARTER_RUNTIME_AI_CONNECTING && id_matches && !accepted) {
        ESP_LOGE(TAG, "AI start_session returned an unsupported audio profile");
        cJSON_Delete(root);
        finish_session(ESP_ERR_NOT_SUPPORTED);
    } else if (state == STARTER_RUNTIME_AI_CONNECTING && accepted) {
        /* 服务端明确接受后才开放麦克风，避免把音频发到未建立的 AI 会话。 */
        cJSON_Delete(root);
        xiaotai_ai_view_init(&s_ai_view);
        if (starter_media_start(STARTER_TIRTC_AI,
                                s_connection_generation) != ESP_OK) {
            finish_session(ESP_ERR_INVALID_STATE);
            return;
        }
        (void)xiaotai_runtime_media_started(&s_session, session_generation());
        product_set_phase(STARTER_AI_UI_LISTENING);
        publish_state();
        diagnostic_event("AI ready ms", (int)(now_ms() - s_diagnostic_ai_started_ms));
    } else if (state == STARTER_RUNTIME_AI_ACTIVE) {
        if (xiaotai_ai_view_apply(&s_ai_view, event->text,
                                  (uint32_t)now_ms())) {
            product_set_emotion(s_ai_view.emotion);
            product_set_phase(product_phase_from_ai_view(s_ai_view.phase));
        }
        const cJSON *method = cJSON_GetObjectItemCaseSensitive(root, "method");
        const cJSON *params = cJSON_GetObjectItemCaseSensitive(root, "params");
        if (cJSON_IsString(method) && method->valuestring != NULL) {
            if (strcmp(method->valuestring, "caption") == 0 &&
                cJSON_IsObject(params)) {
                const cJSON *caption_type = cJSON_GetObjectItemCaseSensitive(
                    params, "caption_type");
                const cJSON *text = cJSON_GetObjectItemCaseSensitive(params, "text");
                const cJSON *mode = cJSON_GetObjectItemCaseSensitive(params, "mode");
                const cJSON *final = cJSON_GetObjectItemCaseSensitive(params, "is_final");
                const cJSON *emotion = cJSON_GetObjectItemCaseSensitive(params, "emotion");
                if (cJSON_IsNumber(caption_type) && cJSON_IsString(text) &&
                    text->valuestring != NULL) {
                    bool is_ai = caption_type->valueint == 1;
                    bool is_final = cJSON_IsBool(final) && cJSON_IsTrue(final);
                    bool append = cJSON_IsNumber(mode) && mode->valueint == 1;
                    if (is_final) diagnostic_event(is_ai ? "AI caption final" : "ASR caption final", 0);
                    product_set_caption(is_ai,
                                        is_final,
                                        text->valuestring,
                                        append,
                                        cJSON_IsString(emotion) ? emotion->valuestring : NULL);
                    product_set_phase(is_ai ? STARTER_AI_UI_SPEAKING :
                                      (is_final ? STARTER_AI_UI_THINKING :
                                                  STARTER_AI_UI_LISTENING));
                }
            } else if (strcmp(method->valuestring, "round_start") == 0) {
                product_set_phase(STARTER_AI_UI_SPEAKING);
            } else if (strcmp(method->valuestring, "round_end") == 0) {
                product_set_phase(STARTER_AI_UI_LISTENING);
            } else if ((strcmp(method->valuestring, "call_intent") == 0 ||
                        strcmp(method->valuestring, "ai_call_intent") == 0 ||
                        strcmp(method->valuestring, "device_action") == 0) &&
                       cJSON_IsObject(params)) {
                char target_id[65] = "";
                char target_name[65] = "";
                char channel[16] = "";
                char media[16] = "";
                uint8_t contact_index = 0;
                bool legacy_call_intent = strcmp(method->valuestring, "device_action") != 0;
                diagnostic_event("call tool received", legacy_call_intent ? 0 : 1);
                contact_match_t match = parse_ai_call_action(
                    params, legacy_call_intent, target_id, sizeof(target_id),
                    target_name, sizeof(target_name), channel, sizeof(channel),
                    media, sizeof(media), &contact_index);
                bool is_device_action = !legacy_call_intent;
                if (match == CONTACT_MATCH_UNIQUE) {
                    diagnostic_event("call tool accepted", 0);
                    if (is_device_action) {
                        send_device_action_result(id, true, "accepted", "正在发起语音呼叫。");
                    }
                    const char end[] =
                        "{\"jsonrpc\":\"2.0\",\"method\":\"end_session\"}";
                    (void)starter_tirtc_send_command(AI_COMMAND,
                                                     end,
                                                     sizeof(end) - 1U);
                    ESP_LOGI(TAG,
                             "AI call accepted: target=%s channel=%s contact-index=%u",
                             target_name[0] != '\0' ? target_name : target_id,
                             channel[0] != '\0' ? channel : "auto",
                             (unsigned)contact_index);
                    cJSON_Delete(root);
                    finish_session(0);
                    dial_contact(contact_index, false);
                } else {
                    diagnostic_event("call tool rejected", (int)match);
                    const char *feedback = media[0] != '\0' &&
                                           strcmp(media, "audio") != 0 &&
                                           strcmp(media, "voice") != 0
                        ? "小钛目前只支持语音呼叫。"
                        : (match == CONTACT_MATCH_AMBIGUOUS
                               ? "找到多个同名联系人，请说得更具体。"
                               : "通讯录里没有找到这个联系人。");
                    if (is_device_action) {
                        send_device_action_result(id, false,
                                                  match == CONTACT_MATCH_AMBIGUOUS
                                                      ? "ambiguous" : "not_found",
                                                  feedback);
                    }
                    product_set_caption(true, true, feedback, false, "confused");
                    product_set_phase(STARTER_AI_UI_LISTENING);
                    ESP_LOGW(TAG,
                             "AI call intent rejected by local contacts: match=%d",
                             (int)match);
                    cJSON_Delete(root);
                }
                return;
            } else if (strcmp(method->valuestring, "end_session") == 0) {
                /* The AI service can end an idle conversation autonomously.
                 * Keep a distinct record so an expected remote timeout is not
                 * mistaken for a local UI or media failure. */
                ESP_LOGI(TAG,
                         "AI session ended by remote end_session/idle policy generation=%lu",
                         (unsigned long)s_connection_generation);
                cJSON_Delete(root);
                begin_ai_end_drain(AI_END_FINAL_AUDIO_ARRIVAL_MS,
                                   AI_END_DRAIN_TIMEOUT_MS,
                                   false);
                return;
            } else {
                diagnostic_event("AI unknown method/params", 0);
            }
        }
        cJSON_Delete(root);
    } else {
        cJSON_Delete(root);
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
    cJSON *root = event->text == NULL
                      ? NULL
                      : cJSON_ParseWithLength(event->text, event->length);
    const cJSON *type = root == NULL
                            ? NULL
                            : cJSON_GetObjectItemCaseSensitive(root, "type");
    const char *signal = cJSON_IsString(type) && type->valuestring != NULL
                             ? type->valuestring : "";
    if (strcmp(signal, "unbind") == 0) {
        cJSON_Delete(root);
        ESP_LOGW(TAG, "device was unbound; clearing local credentials and restarting");
        finish_session(0);
        if (runtime_config_clear_tirtc() != ESP_OK) {
            ESP_LOGE(TAG, "cannot clear stored device credentials");
        }
        vTaskDelay(pdMS_TO_TICKS(300));
        esp_restart();
    }
    if (strcmp(signal, "callers_update") == 0 ||
        strcmp(signal, "contacts_update") == 0) {
        cJSON_Delete(root);
        refresh_contacts();
        return;
    }
    if (strcmp(signal, "room_assignment_changed") == 0) {
        s_room_sync_due_ms = now_ms();
        cJSON_Delete(root);
        return;
    }
    const cJSON *payload = cJSON_IsObject(root)
                               ? cJSON_GetObjectItemCaseSensitive(root, "payload")
                               : NULL;
    const cJSON *channel = cJSON_IsObject(root)
                               ? cJSON_GetObjectItemCaseSensitive(root, "channel")
                               : NULL;
    bool wechat = cJSON_IsString(channel) && channel->valuestring != NULL &&
                  strcmp(channel->valuestring, "wx") == 0;
    if (wechat && strcmp(signal, "call_incoming") == 0 && !s_voip_profile_ready) {
        ESP_LOGW(TAG, "ignoring WeChat call before VoIP profile readiness");
        cJSON_Delete(root);
        request_device_profile();
        return;
    }
    if (strcmp(signal, "room_cancel") == 0 ||
        strcmp(signal, "call_cancel") == 0 ||
        strcmp(signal, "call_reject") == 0) {
        bool rejected = strcmp(signal, "call_reject") == 0;
        char room[129];
        bool got_room = copy_json_string(payload,
                                         wechat ? "wx_room_id" : "room_id",
                                         room, sizeof(room), false);
        cJSON_Delete(root);
        if (call_signal_matches_room(got_room, room, s_call_room_id)) {
            starter_runtime_state_t state = session_state();
            if (session_incoming_pending()) {
                (void)xiaotai_runtime_cancel_incoming(&s_session, NULL);
                s_call_room_id[0] = '\0';
                s_call_peer_id[0] = '\0';
                s_call_peer_name[0] = '\0';
                s_call_wx_app_id[0] = '\0';
                s_call_wx_model_id[0] = '\0';
                s_call_connect_peer[0] = '\0';
                s_call_connect_token[0] = '\0';
                s_call_wx_session_token[0] = '\0';
                s_call_wx_payload[0] = '\0';
                s_call_id[0] = '\0';
                s_call_wechat = false;
#if CONFIG_IDF_TARGET_ESP32P4
                s_call_video = false;
                s_call_camera_enabled = false;
#endif
                product_set_call(false, false, "", false);
                diagnostic_event("incoming cancelled", rejected);
            } else if (state == STARTER_RUNTIME_CALL_INCOMING ||
                state == STARTER_RUNTIME_CALL_CONNECTING ||
                state == STARTER_RUNTIME_CALL_ACTIVE) {
                finish_call_session(0,
                                    rejected ? "对方拒接"
                                             : (state == STARTER_RUNTIME_CALL_INCOMING
                                                    ? "来电已取消"
                                                    : "对方已挂断"));
            }
        }
        return;
    }
    if (strcmp(signal, "callee_answered") == 0 && cJSON_IsObject(payload) &&
        !wechat) {
        char room[129] = "";
        char callee[65] = "";
        (void)copy_json_string(payload, "room_id", room, sizeof(room), false);
        (void)copy_json_string(payload, "callee_id", callee, sizeof(callee), false);
#if CONFIG_IDF_TARGET_ESP32P4
        if (s_call_video) configure_remote_video_presentation(payload, false);
#endif
        cJSON_Delete(root);
        starter_runtime_state_t state = session_state();
        bool matched = state == STARTER_RUNTIME_CALL_CONNECTING &&
                       !s_call_wechat && s_call_outgoing &&
                       room[0] != '\0' && strcmp(room, s_call_room_id) == 0;
        ESP_LOGI(TAG,
                 "[DEBUG-call] callee_answered room=%s matched=%d p2p=%d",
                 room[0] != '\0' ? room : "-", matched ? 1 : 0,
                 s_call_p2p_connected ? 1 : 0);
        if (matched) {
            s_call_peer_answered = true;
            if (callee[0] != '\0') {
                (void)snprintf(s_call_peer_id, sizeof(s_call_peer_id), "%s", callee);
            }
            arm_session_timeout(CALL_CONNECT_TIMEOUT_MS);
            maybe_activate_outgoing_device_call("cloud-answer");
        }
        return;
    }
    if (strcmp(signal, "call_incoming") != 0 || !cJSON_IsObject(payload)) {
        cJSON_Delete(root);
        return;
    }

    starter_runtime_state_t state = session_state();
    char room[129] = "";
    char peer[1024] = "";
    char token[1024] = "";
    char peer_name[65] = "";
    char wx_open_id[65] = "";
    char call_id[65] = "";
    char from_device[65] = "";
    char room_type[16] = "";
    bool fields_ok;
    if (wechat) {
        fields_ok = copy_json_string(payload, "wx_room_id", room, sizeof(room), true) &&
                    copy_json_string(payload, "peer_id", peer, sizeof(peer), true) &&
                    copy_json_string(payload, "token", token, sizeof(token), true);
        (void)copy_json_string(payload, "wx_user_openid", wx_open_id,
                               sizeof(wx_open_id), false);
        (void)copy_json_string(payload, "wx_user_remark", peer_name,
                               sizeof(peer_name), false);
        (void)copy_json_string(payload, "wx_call_id", call_id,
                               sizeof(call_id), false);
        (void)copy_json_string(payload, "wx_from", from_device,
                               sizeof(from_device), false);
        (void)copy_json_string(payload, "wx_room_type", room_type,
                               sizeof(room_type), false);
    } else {
        fields_ok = copy_json_string(payload, "room_id", room, sizeof(room), true) &&
                    copy_json_string(payload, "caller_id", peer, sizeof(peer), true);
        (void)copy_json_string(payload, "caller_name", peer_name,
                               sizeof(peer_name), false);
        (void)copy_json_string(payload, "call_type", room_type,
                               sizeof(room_type), false);
    }
    if (!fields_ok) {
        cJSON_Delete(root);
        return;
    }

    /* 微信外呼的 MQTT 回铃是同一流程，不再弹一次“来电”。 */
    bool outgoing_wechat = wechat && state == STARTER_RUNTIME_CALL_CONNECTING &&
                           s_call_wechat && s_call_outgoing &&
                           (from_device[0] == '\0' ||
                            strcmp(from_device, s_device_id) == 0) &&
                           (s_call_id[0] == '\0' || call_id[0] == '\0' ||
                            strcmp(s_call_id, call_id) == 0);

    /* 已取消外呼的迟到回推绝不能重新显示为用户来电。 */
    bool orphaned_outgoing_wechat = wechat && !outgoing_wechat &&
                                    from_device[0] != '\0' &&
                                    strcmp(from_device, s_device_id) == 0;
    if (orphaned_outgoing_wechat) {
        char app_id[65] = "";
        char model_id[65] = "";
        char server_token[257] = "";
        char wx_payload[513] = "";
        (void)copy_json_string(payload, "wx_app_id", app_id,
                               sizeof(app_id), false);
        (void)copy_json_string(payload, "wx_model_id", model_id,
                               sizeof(model_id), false);
        (void)copy_json_string(payload, "wx_server_token", server_token,
                               sizeof(server_token), false);
        (void)copy_json_string(payload, "wx_payload", wx_payload,
                               sizeof(wx_payload), false);
        reject_wechat_values(app_id, model_id, server_token, room,
                             wx_payload, 7);
        cJSON_Delete(root);
        return;
    }

    /* S3 产品只实现语音；明确拒绝视频房间，避免出现“能接听却无视频”的假能力。 */
    bool unsupported_video = room_type[0] != '\0' &&
                             strcmp(room_type, "voice") != 0 &&
                             strcmp(room_type, "audio") != 0;
#if CONFIG_IDF_TARGET_ESP32P4
    unsupported_video = unsupported_video && strcmp(room_type, "video") != 0;
#endif
    if (unsupported_video) {
        if (wechat) {
            char app_id[65] = "";
            char model_id[65] = "";
            char server_token[257] = "";
            char wx_payload[513] = "";
            (void)copy_json_string(payload, "wx_app_id", app_id,
                                   sizeof(app_id), false);
            (void)copy_json_string(payload, "wx_model_id", model_id,
                                   sizeof(model_id), false);
            (void)copy_json_string(payload, "wx_server_token", server_token,
                                   sizeof(server_token), false);
            (void)copy_json_string(payload, "wx_payload", wx_payload,
                                   sizeof(wx_payload), false);
            reject_wechat_values(app_id, model_id, server_token, room,
                                 wx_payload, 7);
        } else {
            char body[256];
            (void)snprintf(body, sizeof(body),
                           "{\"room_id\":\"%s\",\"reason\":\"unsupported_media\"}",
                           room);
            (void)platform_client_request(PLATFORM_SERVICE_CALL,
                                          "/v1/call/reject", body, NULL, NULL);
        }
        cJSON_Delete(root);
        return;
    }

    bool duplicate = (session_incoming_pending() ||
                      (state >= STARTER_RUNTIME_CALL_INCOMING &&
                       state <= STARTER_RUNTIME_CALL_ACTIVE)) &&
                     s_call_room_id[0] != '\0' &&
                     strcmp(s_call_room_id, room) == 0;
    if (duplicate) {
        cJSON_Delete(root);
        return;
    }
    xiaotai_session_owner_t incoming_owner = wechat
                                                  ? XIAOTAI_OWNER_WECHAT_VOIP
                                                  : XIAOTAI_OWNER_DEVICE_CALL;
    if (!outgoing_wechat &&
        ((!wechat && s_call_cleanup_room[0] != '\0') ||
         !xiaotai_runtime_offer_incoming(&s_session, incoming_owner,
                                        (uint32_t)now_ms(),
                                        CALL_PENDING_TIMEOUT_MS))) {
        if (wechat) {
            char app_id[65] = "";
            char model_id[65] = "";
            char server_token[257] = "";
            char wx_payload[513] = "";
            (void)copy_json_string(payload, "wx_app_id", app_id,
                                   sizeof(app_id), false);
            (void)copy_json_string(payload, "wx_model_id", model_id,
                                   sizeof(model_id), false);
            (void)copy_json_string(payload, "wx_server_token", server_token,
                                   sizeof(server_token), false);
            (void)copy_json_string(payload, "wx_payload", wx_payload,
                                   sizeof(wx_payload), false);
            reject_wechat_values(app_id, model_id, server_token, room,
                                 wx_payload, 8);
        } else {
            char body[224];
            (void)snprintf(body, sizeof(body),
                           "{\"room_id\":\"%s\",\"reason\":\"busy\"}", room);
            (void)platform_client_request(PLATFORM_SERVICE_CALL,
                                          "/v1/call/reject", body, NULL, NULL);
        }
        cJSON_Delete(root);
        return;
    }
    if (!outgoing_wechat) s_call_outgoing = false;
    s_call_wechat = wechat;
#if CONFIG_IDF_TARGET_ESP32P4
    if (!s_call_outgoing) {
        /* Mini-program incoming calls may omit the outbound correlation field
         * wx_room_type. P4 advertises no_video=false in its VoIP profile, so
         * that incoming default is video. Explicit voice/audio stays audio;
         * outbound callbacks retain the user's selection above. */
        s_call_video = strcmp(room_type, "video") == 0 ||
                       (wechat && room_type[0] == '\0');
        s_call_camera_enabled = s_call_video;
        if (s_call_video) configure_remote_video_presentation(payload, wechat);
    }
#endif
    (void)snprintf(s_call_room_id, sizeof(s_call_room_id), "%s", room);
    (void)snprintf(s_call_peer_id, sizeof(s_call_peer_id), "%.64s",
                   wechat && wx_open_id[0] != '\0' ? wx_open_id : peer);
    (void)snprintf(s_call_peer_name, sizeof(s_call_peer_name), "%.64s",
                   peer_name[0] == '\0'
                       ? (wechat && wx_open_id[0] != '\0'
                              ? wx_open_id
                              : (wechat ? "微信联系人" : peer))
                       : peer_name);
    if (wechat) {
        (void)snprintf(s_call_connect_peer,
                       sizeof(s_call_connect_peer), "%s", peer);
        (void)snprintf(s_call_connect_token,
                       sizeof(s_call_connect_token), "%s", token);
        (void)copy_json_string(payload, "wx_app_id", s_call_wx_app_id,
                               sizeof(s_call_wx_app_id), false);
        (void)copy_json_string(payload, "wx_model_id", s_call_wx_model_id,
                               sizeof(s_call_wx_model_id), false);
        (void)copy_json_string(payload, "wx_server_token", s_call_wx_session_token,
                               sizeof(s_call_wx_session_token), false);
        (void)copy_json_string(payload, "wx_payload", s_call_wx_payload,
                               sizeof(s_call_wx_payload), false);
    }
    cJSON_Delete(root);
    if (outgoing_wechat) {
        product_snapshot_reset();
        product_set_call(false, wechat, s_call_peer_name, false);
        publish_state();
        if (!queue_event(&(runtime_event_t){.type = EVENT_VOIP_CONNECT})) {
            finish_call_session(ESP_ERR_TIMEOUT, "呼叫繁忙");
        }
    } else {
        product_set_call(true, wechat, s_call_peer_name, false);
        diagnostic_event("incoming call", wechat);
    }
}

static void room_http_response(const char *body, void *user_data)
{
    uint32_t stage = (uint32_t)(uintptr_t)user_data;
    if (!queue_http_result(EVENT_ROOM_HTTP, 0, stage, body)) {
        atomic_store_explicit(&s_room_http_delivery_failed_stage, stage,
                              memory_order_release);
    }
}

static bool room_request(room_http_stage_t stage, const char *path,
                         const char *body)
{
    if (s_room_http_inflight || !platform_client_ready()) return false;
    esp_err_t err = platform_client_request_timeout(
        PLATFORM_SERVICE_CALL, path, body, 8000U, room_http_response,
        (void *)(uintptr_t)stage);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "room request %s rejected: %s", path, esp_err_to_name(err));
        return false;
    }
    s_room_http_inflight = true;
    return true;
}

static void room_new_session_id(void)
{
    for (unsigned i = 0; i < 8U; ++i) {
        (void)snprintf(s_room_session_id + i * 8U,
                       sizeof(s_room_session_id) - i * 8U,
                       "%08lx", (unsigned long)esp_random());
    }
    s_room_session_id[64] = '\0';
}

static void room_request_assignment(void)
{
    /* Assignment reads are edge-triggered by MQTT or an explicit user entry.
     * Consume the pending edge before dispatch so failures cannot turn into a
     * hidden timer-based polling loop. */
    s_room_sync_due_ms = 0;
    (void)room_request(ROOM_HTTP_ASSIGNMENT,
                       "/v1/call/group/device/assignment", NULL);
}

static void room_request_presence(const char *state)
{
    if (s_room_id[0] == '\0' || s_room_assignment_version <= 0 ||
        s_room_session_id[0] == '\0') return;
    char body[320];
    (void)snprintf(body, sizeof(body),
                   "{\"room_id\":\"%s\",\"assignment_version\":%lld,"
                   "\"session_id\":\"%s\",\"state\":\"%s\"}",
                   s_room_id, (long long)s_room_assignment_version,
                   s_room_session_id, state);
    if (!room_request(ROOM_HTTP_PRESENCE,
                      "/v1/call/group/device/presence", body) &&
        strcmp(state, "joined") != 0 && strcmp(state, "connecting") != 0) {
        (void)snprintf(s_room_pending_presence_body,
                       sizeof(s_room_pending_presence_body), "%s", body);
    }
}

static void room_stop_connection(const char *presence, int error)
{
    starter_runtime_state_t state = session_state();
    if (state != STARTER_RUNTIME_ROOM_CONNECTING &&
        state != STARTER_RUNTIME_ROOM_ACTIVE) return;
    starter_media_set_uplink_enabled(false);
    s_room_ptt = false;
    if (starter_tirtc_connected()) {
        static const char off[] =
            "{\"jsonrpc\":\"2.0\",\"method\":\"set_mic_state\","
            "\"params\":{\"mic_state\":\"off\"}}";
        static const char leave[] =
            "{\"jsonrpc\":\"2.0\",\"method\":\"leave_room\"}";
        (void)starter_tirtc_send_command(ROOM_COMMAND, off, sizeof(off) - 1U);
        (void)starter_tirtc_send_command(ROOM_COMMAND, leave, sizeof(leave) - 1U);
    }
    bool resume_after_foreground = presence != NULL &&
                                   strcmp(presence, "suspended") == 0 &&
                                   s_room_desired;
    s_room_joined = false;
    finish_session(error);
    product_set_room(s_room_desired ? STARTER_ROOM_ASSIGNED : STARTER_ROOM_NONE,
                     s_room_desired ? "等待恢复" : "");
    if (presence != NULL) room_request_presence(presence);
    s_room_session_id[0] = '\0';
    /* The next foreground owner is installed by the same runtime event.  Do
     * not reconnect here: remember the suspended assignment and let the main
     * loop restore it only after the foreground owner really returns to idle. */
    s_room_resume_pending = resume_after_foreground;
}

static void room_set_foreground(bool active)
{
    if (s_room_page_active == active) return;
    s_room_page_active = active;
    ++s_room_page_epoch;
    if (!active) {
        s_room_ptt = false;
        room_stop_connection("left", 0);
        s_room_resume_pending = false;
    }
}

static bool room_token_is_current(void)
{
    return s_room_page_active && s_room_token_epoch == s_room_page_epoch;
}

static void room_request_token(void)
{
    if (!s_room_page_active || !s_room_desired || s_room_id[0] == '\0' ||
        s_room_pending_action == ROOM_ACTION_LEAVE ||
        s_room_mutation_action == ROOM_ACTION_LEAVE ||
        s_room_assignment_version <= 0 ||
        s_room_pending_presence_body[0] != '\0') return;
    s_room_resume_pending = false;
    if (s_room_session_id[0] == '\0') room_new_session_id();
    char body[300];
    (void)snprintf(body, sizeof(body),
                   "{\"room_id\":\"%s\",\"assignment_version\":%lld,"
                   "\"session_id\":\"%s\"}", s_room_id,
                   (long long)s_room_assignment_version, s_room_session_id);
    s_room_token_epoch = s_room_page_epoch;
    if (room_request(ROOM_HTTP_TOKEN,
                     "/v1/call/group/device/connect-token", body)) {
        product_set_room(STARTER_ROOM_CONNECTING, "正在连接");
    }
}

static void handle_room_http(const runtime_event_t *event)
{
    s_room_http_inflight = false;
    /* Validate the scope before either success or failure changes the UI. */
    if (event->command == ROOM_HTTP_TOKEN && !room_token_is_current()) return;
    if (event->command == ROOM_HTTP_MUTATION) s_room_mutation_action = 0;
    cJSON *root = event->text == NULL ? NULL :
        cJSON_ParseWithLength(event->text, event->length);
    bool ok = response_ok(root);
    const cJSON *data = ok
                            ? cJSON_GetObjectItemCaseSensitive(root, "data") : NULL;
    if (!ok || (event->command != ROOM_HTTP_PRESENCE && !cJSON_IsObject(data))) {
        const cJSON *message = cJSON_IsObject(root)
            ? cJSON_GetObjectItemCaseSensitive(root, "msg") : NULL;
        if (!cJSON_IsString(message) && cJSON_IsObject(root))
            message = cJSON_GetObjectItemCaseSensitive(root, "message");
        const char *text = cJSON_IsString(message) && message->valuestring
                               ? message->valuestring : "房间请求失败";
        ESP_LOGW(TAG, "room HTTP stage=%lu failed: %s",
                 (unsigned long)event->command, text);
        if (event->command == ROOM_HTTP_MUTATION)
            product_set_room(STARTER_ROOM_ERROR, text);
        else if (event->command == ROOM_HTTP_TOKEN) {
            room_stop_connection("connect_failed", ESP_FAIL);
            product_set_room(STARTER_ROOM_ERROR, text);
        } else if (event->command == ROOM_HTTP_PRESENCE)
            room_stop_connection(NULL, ESP_FAIL);
        cJSON_Delete(root);
        return;
    }

    if (event->command == ROOM_HTTP_ASSIGNMENT) {
        char room_id[65] = "", room_code[7] = "", desired[16] = "";
        char assignment_state[24] = "", server_session[65] = "";
        bool valid = copy_json_string(data, "room_id", room_id,
                                      sizeof(room_id), false) &&
                     copy_json_string(data, "room_code", room_code,
                                      sizeof(room_code), false) &&
                     copy_json_string(data, "desired_state", desired,
                                      sizeof(desired), false) &&
                     copy_json_string(data, "state", assignment_state,
                                      sizeof(assignment_state), false) &&
                     copy_json_string(data, "session_id", server_session,
                                      sizeof(server_session), false);
        const cJSON *version_item = cJSON_GetObjectItemCaseSensitive(
            data, "assignment_version");
        int64_t version = cJSON_IsNumber(version_item)
                              ? (int64_t)version_item->valuedouble : 0;
        bool desired_join = strcmp(desired, "joined") == 0;
        bool changed = version != s_room_assignment_version ||
                       strcmp(room_id, s_room_id) != 0;
        if (!valid || version < 0 || (desired_join &&
            (room_id[0] == '\0' || strlen(room_code) != 6U))) {
            product_set_room(STARTER_ROOM_ERROR, "房间数据无效");
            cJSON_Delete(root);
            return;
        }
        if (changed && (session_state() == STARTER_RUNTIME_ROOM_CONNECTING ||
                        session_state() == STARTER_RUNTIME_ROOM_ACTIVE)) {
            room_stop_connection("suspended", 0);
        }
        s_room_assignment_version = version;
        s_room_desired = desired_join;
        (void)snprintf(s_room_id, sizeof(s_room_id), "%s", room_id);
        (void)snprintf(s_room_code, sizeof(s_room_code), "%s", room_code);
        bool recover_live_lease =
            (strcmp(assignment_state, "connecting") == 0 ||
             strcmp(assignment_state, "joined") == 0) &&
            strlen(server_session) >= 8U;
        (void)snprintf(s_room_session_id, sizeof(s_room_session_id), "%s",
                       recover_live_lease ? server_session : "");
        if (s_product_mutex != NULL &&
            xSemaphoreTake(s_product_mutex, pdMS_TO_TICKS(50)) == pdTRUE) {
            const cJSON *password_set = cJSON_GetObjectItemCaseSensitive(data, "password_set");
            const cJSON *online_count = cJSON_GetObjectItemCaseSensitive(data, "online_count");
            s_product_snapshot.room_password_set = cJSON_IsTrue(password_set);
            s_product_snapshot.room_online_count = cJSON_IsNumber(online_count)
                ? (uint8_t)(online_count->valueint < 0 ? 0 : online_count->valueint) : 0;
            xSemaphoreGive(s_product_mutex);
        }
        if (!desired_join) {
            s_room_resume_pending = false;
            s_room_id[0] = '\0'; s_room_code[0] = '\0';
            product_set_room(STARTER_ROOM_NONE, "");
        } else {
            starter_runtime_state_t state = session_state();
            if (s_room_page_active && state == STARTER_RUNTIME_H5_ACTIVE) {
                finish_session(0);
                state = STARTER_RUNTIME_WAITING;
            }
            if (state == STARTER_RUNTIME_WAITING) room_request_token();
            else if (state != STARTER_RUNTIME_ROOM_CONNECTING &&
                     state != STARTER_RUNTIME_ROOM_ACTIVE)
                product_set_room(STARTER_ROOM_ASSIGNED, "当前通话结束后恢复");
        }
    } else if (event->command == ROOM_HTTP_MUTATION) {
        product_set_room(STARTER_ROOM_CONNECTING, "正在同步");
        s_room_sync_due_ms = now_ms();
    } else if (event->command == ROOM_HTTP_TOKEN) {
        ai_credentials_t credentials = {0};
        bool valid = copy_json_string(data, "peer_id", credentials.peer_id,
                                      sizeof(credentials.peer_id), true) &&
                     copy_json_string(data, "token", credentials.token,
                                      sizeof(credentials.token), true);
        const cJSON *heartbeat = cJSON_GetObjectItemCaseSensitive(data, "heartbeat_seconds");
        const cJSON *lease = cJSON_GetObjectItemCaseSensitive(data, "lease_seconds");
        if (!valid) {
            product_set_room(STARTER_ROOM_ERROR, "连接凭证无效");
        } else {
            s_room_heartbeat_seconds = cJSON_IsNumber(heartbeat) && heartbeat->valueint > 0
                                           ? heartbeat->valueint : 15;
            s_room_lease_seconds = cJSON_IsNumber(lease) && lease->valueint >= 3
                                       ? lease->valueint : 45;
            if (!xiaotai_runtime_begin(&s_session, XIAOTAI_OWNER_ROOM, false)) {
                product_set_room(STARTER_ROOM_ERROR, "当前状态无法加入");
                cJSON_Delete(root);
                return;
            }
            starter_tirtc_accept_h5(false);
            starter_media_stop();
            if (!suspend_mqtt_for_external_connect()) {
                finish_session(ESP_ERR_NO_MEM);
                product_set_room(STARTER_ROOM_ERROR, "内存不足");
            } else {
                int rc = starter_tirtc_room_connect(credentials.peer_id,
                                                     credentials.token,
                                                     session_generation());
                if (rc == 0) {
                    arm_session_timeout(ROOM_CONNECT_TIMEOUT_MS);
                    publish_state();
                } else {
                    resume_mqtt_after_external_connect();
                    finish_session(rc);
                    product_set_room(STARTER_ROOM_ERROR, "连接提交失败");
                }
            }
        }
    } else if (event->command == ROOM_HTTP_PRESENCE && s_room_joined) {
        s_room_next_heartbeat_ms = now_ms() + s_room_heartbeat_seconds * 1000;
        s_room_lease_deadline_ms = now_ms() + s_room_lease_seconds * 1000;
    }
    cJSON_Delete(root);
}

static void handle_room_command(const runtime_event_t *event)
{
    if ((event->command & 0xfffeU) != ROOM_COMMAND ||
        event->generation != s_connection_generation || event->text == NULL) return;
    cJSON *root = cJSON_ParseWithLength(event->text, event->length);
    if (!cJSON_IsObject(root)) { cJSON_Delete(root); return; }
    const cJSON *result = cJSON_GetObjectItemCaseSensitive(root, "result");
    const cJSON *id = cJSON_GetObjectItemCaseSensitive(root, "id");
    if (!s_room_joined && cJSON_IsNumber(id) && id->valueint == 1 &&
        cJSON_IsObject(result)) {
        char session[65] = "";
        const cJSON *input = cJSON_GetObjectItemCaseSensitive(result, "input_audio");
        const cJSON *output = cJSON_GetObjectItemCaseSensitive(result, "output_audio");
        const cJSON *ic = cJSON_IsObject(input) ? cJSON_GetObjectItem(input, "codec") : NULL;
        const cJSON *oc = cJSON_IsObject(output) ? cJSON_GetObjectItem(output, "codec") : NULL;
        const cJSON *isr = cJSON_IsObject(input) ? cJSON_GetObjectItem(input, "sample_rate") : NULL;
        const cJSON *osr = cJSON_IsObject(output) ? cJSON_GetObjectItem(output, "sample_rate") : NULL;
        const cJSON *ich = cJSON_IsObject(input) ? cJSON_GetObjectItem(input, "channels") : NULL;
        const cJSON *och = cJSON_IsObject(output) ? cJSON_GetObjectItem(output, "channels") : NULL;
        bool valid = copy_json_string(result, "session_id", session, sizeof(session), true) &&
            cJSON_IsString(ic) &&
            cJSON_IsString(oc) && strcmp(ic->valuestring, "g711a") == 0 &&
            strcmp(oc->valuestring, "g711a") == 0 && cJSON_IsNumber(isr) &&
            cJSON_IsNumber(osr) && isr->valueint == 8000 && osr->valueint == 8000 &&
            cJSON_IsNumber(ich) && cJSON_IsNumber(och) &&
            ich->valueint == 1 && och->valueint == 1;
        if (!valid || starter_media_start(STARTER_TIRTC_ROOM,
                                          s_connection_generation) != ESP_OK) {
            ESP_LOGW(TAG, "join_room response rejected: session/format/media invalid");
            cJSON_Delete(root);
            room_stop_connection("connect_failed", ESP_ERR_INVALID_RESPONSE);
            return;
        }
        s_room_joined = true; s_room_ptt = false;
        (void)xiaotai_runtime_media_started(&s_session, session_generation());
        starter_media_set_uplink_enabled(false);
        publish_state();
        product_set_room(STARTER_ROOM_JOINED, "已加入");
        room_request_presence("joined");
        static const char off[] =
            "{\"jsonrpc\":\"2.0\",\"method\":\"set_mic_state\","
            "\"params\":{\"mic_state\":\"off\"}}";
        static const char snapshot[] =
            "{\"jsonrpc\":\"2.0\",\"method\":\"get_room_snapshot\"}";
        (void)starter_tirtc_send_command(ROOM_COMMAND, off, sizeof(off) - 1U);
        (void)starter_tirtc_send_command(ROOM_COMMAND, snapshot, sizeof(snapshot) - 1U);
    }
    const cJSON *method = cJSON_GetObjectItemCaseSensitive(root, "method");
    const cJSON *params = cJSON_GetObjectItemCaseSensitive(root, "params");
    const cJSON *wire_room = cJSON_IsObject(params)
        ? cJSON_GetObjectItemCaseSensitive(params, "room_id") : NULL;
    if (cJSON_IsString(wire_room) && wire_room->valuestring && wire_room->valuestring[0]) {
        const char *value = wire_room->valuestring;
        const char *separator = strrchr(value, ':');
        if (strcmp(value, s_room_id) != 0 &&
            (separator == NULL || strcmp(separator + 1, s_room_id) != 0)) {
            cJSON_Delete(root);
            return;
        }
    }
    if (cJSON_IsString(method) && strcmp(method->valuestring, "room_closed") == 0) {
        cJSON_Delete(root);
        room_stop_connection("left", 0);
        s_room_resume_pending = false;
        s_room_desired = false;
        s_room_assignment_version = 0;
        s_room_id[0] = '\0';
        s_room_code[0] = '\0';
        s_room_session_id[0] = '\0';
        product_set_room(STARTER_ROOM_NONE, "");
        return;
    }
    if (cJSON_IsString(method) && strcmp(method->valuestring, "room_snapshot") == 0 &&
        cJSON_IsObject(params)) {
        const cJSON *participants = cJSON_GetObjectItemCaseSensitive(params, "participants");
        if (cJSON_IsArray(participants) && s_product_mutex != NULL &&
            xSemaphoreTake(s_product_mutex, pdMS_TO_TICKS(50)) == pdTRUE) {
            memset(s_product_snapshot.room_members, 0,
                   sizeof(s_product_snapshot.room_members));
            int total = cJSON_GetArraySize(participants);
            s_product_snapshot.room_online_count = (uint8_t)(total > 255 ? 255 : total);
            int copied = total > STARTER_ROOM_MEMBERS_MAX ? STARTER_ROOM_MEMBERS_MAX : total;
            s_product_snapshot.room_member_count = (uint8_t)copied;
            for (int i = 0; i < copied; ++i) {
                const cJSON *p = cJSON_GetArrayItem(participants, i);
                char device[65] = "", participant[65] = "", mic[16] = "";
                (void)copy_json_string(p, "device_id", device, sizeof(device), false);
                (void)copy_json_string(p, "participant_id", participant, sizeof(participant), false);
                (void)copy_json_string(p, "mic_state", mic, sizeof(mic), false);
                (void)snprintf(s_product_snapshot.room_members[i].id,
                               sizeof(s_product_snapshot.room_members[i].id), "%s",
                               device[0] ? device : participant);
                s_product_snapshot.room_members[i].self =
                    device[0] && strcmp(device, s_device_id) == 0;
                s_product_snapshot.room_members[i].speaking = strcmp(mic, "speaking") == 0;
            }
            xSemaphoreGive(s_product_mutex);
        }
    }
    if (cJSON_IsString(method) &&
        (strcmp(method->valuestring, "participant_joined") == 0 ||
         strcmp(method->valuestring, "participant_left") == 0 ||
         strcmp(method->valuestring, "participant_mic_state_changed") == 0)) {
        static const char snapshot[] =
            "{\"jsonrpc\":\"2.0\",\"method\":\"get_room_snapshot\"}";
        (void)starter_tirtc_send_command(ROOM_COMMAND, snapshot, sizeof(snapshot) - 1U);
    }
    cJSON_Delete(root);
}

static void handle_room_action(const runtime_event_t *event)
{
    if (event->command == ROOM_ACTION_FOREGROUND) {
        room_set_foreground(event->flag);
        return;
    }
    if (event->command == ROOM_ACTION_SYNC) { s_room_sync_due_ms = now_ms(); return; }
    if (event->command == ROOM_ACTION_PTT) {
        if (!s_room_joined || session_state() != STARTER_RUNTIME_ROOM_ACTIVE) return;
        if (event->flag && !atomic_load_explicit(&s_room_key_pressed,
                                                memory_order_acquire)) return;
        /* Release is unconditional; a failed command must never keep capture
         * open. Admission of a press follows successful wire submission. */
        if (!event->flag) {
            s_room_ptt = false;
            starter_media_set_uplink_enabled(false);
        }
        char body[128];
        (void)snprintf(body, sizeof(body),
            "{\"jsonrpc\":\"2.0\",\"method\":\"set_mic_state\","
            "\"params\":{\"mic_state\":\"%s\"}}", event->flag ? "speaking" : "off");
        int rc = starter_tirtc_send_command(ROOM_COMMAND, body, (uint32_t)strlen(body));
        s_room_ptt = event->flag && rc >= 0 &&
            atomic_load_explicit(&s_room_key_pressed, memory_order_acquire);
        starter_media_set_uplink_enabled(s_room_ptt);
        product_set_room(STARTER_ROOM_JOINED, s_room_ptt ? "正在说话" :
            (event->flag && rc < 0 ? "请求失败，请重试" : "已加入"));
        return;
    }
    if (event->command == s_room_mutation_action ||
        event->command == s_room_pending_action) return;
    if ((event->command == ROOM_ACTION_CREATE ||
         event->command == ROOM_ACTION_JOIN) && s_room_desired) {
        product_set_room(STARTER_ROOM_ASSIGNED, "请先退出当前房间");
        return;
    }
    if (event->command == ROOM_ACTION_LEAVE) {
        /* A queued leave must invalidate a connect-token already in flight. */
        ++s_room_page_epoch;
        s_room_resume_pending = false;
        room_stop_connection(NULL, 0);
        char body[256];
        (void)snprintf(body, sizeof(body),
                       "{\"room_id\":\"%s\",\"assignment_version\":%lld}",
                       s_room_id, (long long)s_room_assignment_version);
        if (!room_request(ROOM_HTTP_MUTATION, "/v1/call/group/device/leave", body)) {
            s_room_pending_action = ROOM_ACTION_LEAVE;
            (void)snprintf(s_room_pending_body, sizeof(s_room_pending_body), "%s", body);
        } else s_room_mutation_action = ROOM_ACTION_LEAVE;
        return;
    }
    if (event->text == NULL) return;
    const char *path = event->command == ROOM_ACTION_CREATE
                           ? "/v1/call/group/device/create"
                           : "/v1/call/group/device/join";
    if (!room_request(ROOM_HTTP_MUTATION, path, event->text)) {
        s_room_pending_action = (room_action_t)event->command;
        (void)snprintf(s_room_pending_body, sizeof(s_room_pending_body), "%s", event->text);
        product_set_room(STARTER_ROOM_CONNECTING, "等待提交");
    } else s_room_mutation_action = (room_action_t)event->command;
}

static void room_try_pending_presence(void)
{
    if (s_room_pending_presence_body[0] == '\0' || s_room_http_inflight) return;
    if (room_request(ROOM_HTTP_PRESENCE,
                     "/v1/call/group/device/presence",
                     s_room_pending_presence_body)) {
        s_room_pending_presence_body[0] = '\0';
    }
}

static void room_try_pending_action(void)
{
    if (s_room_pending_action == 0 || s_room_http_inflight) return;
    const char *path = s_room_pending_action == ROOM_ACTION_CREATE
                           ? "/v1/call/group/device/create"
                       : s_room_pending_action == ROOM_ACTION_JOIN
                           ? "/v1/call/group/device/join"
                           : "/v1/call/group/device/leave";
    if (room_request(ROOM_HTTP_MUTATION, path, s_room_pending_body)) {
        s_room_mutation_action = s_room_pending_action;
        s_room_pending_action = 0;
        s_room_pending_body[0] = '\0';
    }
}

static void handle_wechat_quick_call(void)
{
    uint8_t index = STARTER_PRODUCT_CONTACTS_MAX;
    if (s_product_mutex != NULL &&
        xSemaphoreTake(s_product_mutex, pdMS_TO_TICKS(50)) == pdTRUE) {
        for (uint8_t i = 0; i < s_product_snapshot.contact_count; ++i) {
            if (s_product_snapshot.contacts[i].source == STARTER_CONTACT_WECHAT) {
                index = i; break;
            }
        }
        s_product_snapshot.wechat_contacts_checked = false;
        xSemaphoreGive(s_product_mutex);
    }
    if (index < STARTER_PRODUCT_CONTACTS_MAX) {
        dial_contact(index, false);
        return;
    }
    s_wechat_quick_pending = true;
    s_contacts_check_deadline_ms = now_ms() + CONTACTS_CHECK_TIMEOUT_MS;
    s_contacts_retry_due_ms = now_ms();
    refresh_contacts();
}

static void dial_first_contact(void)
{
    if (s_product_mutex == NULL ||
        xSemaphoreTake(s_product_mutex, pdMS_TO_TICKS(50)) != pdTRUE) return;
    bool empty = s_product_snapshot.contact_count == 0U;
    if (empty) ++s_product_snapshot.contact_guide_sequence;
    xSemaphoreGive(s_product_mutex);
    if (empty) end_ai_session();
    else dial_contact(0, false);
}

static void handle_main_key(bool double_click)
{
    /* Incoming notifications overlay any current media owner. */
    if (session_incoming_pending()) {
        if (double_click) reject_or_hangup_call(true);
        else accept_call();
        return;
    }
    starter_runtime_state_t state = session_state();
    if (state == STARTER_RUNTIME_CALL_INCOMING ||
        state == STARTER_RUNTIME_CALL_CONNECTING ||
        state == STARTER_RUNTIME_CALL_ACTIVE || state == XIAOTAI_STATE_CALL_ENDING) {
        if (!double_click && state != XIAOTAI_STATE_CALL_ENDING)
            reject_or_hangup_call(false);
        return;
    }
    if (s_room_page_active || state == STARTER_RUNTIME_ROOM_CONNECTING ||
        state == STARTER_RUNTIME_ROOM_ACTIVE) {
        if (!double_click) {
            room_set_foreground(false);
            product_set_room(s_room_desired ? STARTER_ROOM_ASSIGNED : STARTER_ROOM_NONE,
                             "已结束本机对讲，房间已保留");
        }
        return;
    }
    if (double_click) {
        /* Platform order is authoritative, including device contacts. */
        dial_first_contact();
    } else if (state == STARTER_RUNTIME_AI_CONNECTING ||
               state == STARTER_RUNTIME_AI_ACTIVE) {
        end_ai_session();
    } else {
        begin_ai_session(0);
    }
}

static void recover_product_http_delivery_failures(void)
{
    /* Callbacks cannot mutate business state. Recover on the owner task even
     * when no response event could be allocated or queued. No assignment poll
     * is scheduled: another MQTT/user edge is required after a failed read. */
    uint32_t stage = atomic_exchange_explicit(&s_room_http_delivery_failed_stage,
                                              0, memory_order_acq_rel);
    if (stage != 0U) {
        const runtime_event_t failed = {.type = EVENT_ROOM_HTTP, .command = stage};
        handle_room_http(&failed);
    }
    if (atomic_exchange_explicit(&s_contacts_delivery_failed, false,
                                  memory_order_acq_rel)) {
        const runtime_event_t failed = {.type = EVENT_CONTACTS_RESULT};
        handle_contacts_result(&failed);
    }
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
        if (atomic_exchange_explicit(&s_room_release_required, false,
                                     memory_order_acq_rel) && s_room_ptt) {
            const runtime_event_t release = {.type = EVENT_ROOM_ACTION,
                                             .command = ROOM_ACTION_PTT,
                                             .flag = false};
            handle_room_action(&release);
        }
        if (recover_voip_profile_delivery_failure(now_ms())) {
            ESP_LOGW(TAG, "VoIP profile response was not delivered; retrying");
        }
        recover_product_http_delivery_failures();
        service_call_cleanup();
        service_ai_end_drain();
        if (s_mqtt_suspended_for_connect && s_mqtt_resume_due_ms != 0 &&
            now_ms() >= s_mqtt_resume_due_ms) {
            resume_mqtt_after_external_connect();
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
                if (event.mode == STARTER_TIRTC_AI) {
                    handle_ai_command(&event);
                } else if (event.mode == STARTER_TIRTC_ROOM) {
                    handle_room_command(&event);
                } else {
                    handle_call_command(&event);
                }
                break;
            case EVENT_AI_START:
                begin_ai_session(event.generation);
                break;
            case EVENT_MAIN_KEY:
                handle_main_key(event.flag);
                break;
            case EVENT_AI_STOP:
                s_ai_start_pending = false;
                s_ai_ready_deadline_ms = 0;
                end_ai_session();
                break;
            case EVENT_AI_TOKEN:
                handle_ai_token(&event);
                break;
            case EVENT_PLATFORM_SIGNAL:
                handle_platform_signal(&event);
                break;
            case EVENT_PLATFORM_ONLINE:
                /* MQTT token/会话换代后，服务端 profile 必须重新建立。 */
                s_voip_profile_ready = false;
                request_device_profile();
                break;
            case EVENT_VOIP_PROFILE:
                handle_voip_profile(&event);
                break;
            case EVENT_VOIP_CONNECT:
                connect_voip();
                break;
            case EVENT_VOIP_CONNECT_RESULT:
                handle_voip_connect_result(&event);
                break;
            case EVENT_CONTACTS_REFRESH:
                refresh_contacts();
                break;
            case EVENT_CONTACTS_RESULT:
                handle_contacts_result(&event);
                break;
            case EVENT_CALL_DIAL:
                dial_contact((uint8_t)event.command, event.flag);
                break;
            case EVENT_CALL_ACCEPT:
                accept_call();
                break;
            case EVENT_CALL_REJECT:
                reject_or_hangup_call(true);
                break;
            case EVENT_CALL_HANGUP:
                reject_or_hangup_call(false);
                break;
            case EVENT_CALL_MIC_MUTE:
                starter_runtime_state_t state = session_state();
                if (!session_incoming_pending() &&
                    state >= STARTER_RUNTIME_CALL_INCOMING &&
                    state <= STARTER_RUNTIME_CALL_ACTIVE) {
                    starter_media_set_microphone_muted(event.flag);
                    product_set_call(
                        state == STARTER_RUNTIME_CALL_INCOMING,
                        s_call_wechat,
                        s_call_peer_name,
                        event.flag);
                }
                break;
            case EVENT_CALL_HTTP:
                handle_call_http(&event);
                break;
            case EVENT_WECHAT_QUICK_CALL:
                handle_wechat_quick_call();
                break;
            case EVENT_ROOM_ACTION:
                handle_room_action(&event);
                break;
            case EVENT_ROOM_HTTP:
                handle_room_http(&event);
                break;
#if CONFIG_IDF_TARGET_ESP32P4
            case EVENT_CALL_CAMERA:
                if (event.generation == session_generation() && s_call_video &&
                    session_state() == STARTER_RUNTIME_CALL_ACTIVE &&
                    starter_media_set_camera_enabled(s_connection_generation, event.flag) == ESP_OK) {
                    s_call_camera_enabled = event.flag;
                    product_set_call(false, s_call_wechat, s_call_peer_name,
                                     starter_media_status().microphone_muted);
                }
                break;
#endif
            default:
                break;
            }
            release_event(&event);
        }
        int64_t current_ms = now_ms();
        retry_pending_ai_start(current_ms);
        if (s_wechat_quick_pending && s_contacts_check_deadline_ms != 0 &&
            current_ms >= s_contacts_check_deadline_ms) {
            ESP_LOGW(TAG, "微信联系人检查超时; showing QR fallback");
            s_wechat_quick_pending = false;
            s_contacts_retry_due_ms = 0;
            s_contacts_check_deadline_ms = 0;
            if (s_product_mutex != NULL &&
                xSemaphoreTake(s_product_mutex, pdMS_TO_TICKS(50)) == pdTRUE) {
                s_product_snapshot.wechat_contacts_checked = true;
                s_product_snapshot.wechat_contact_count = 0;
                xSemaphoreGive(s_product_mutex);
            }
        } else if (s_wechat_quick_pending && !s_contacts_inflight &&
                   s_contacts_retry_due_ms != 0 &&
                   current_ms >= s_contacts_retry_due_ms) {
            refresh_contacts();
        }
        room_try_pending_presence();
        room_try_pending_action();
        if (s_room_page_active && s_room_resume_pending && !s_room_http_inflight &&
            s_room_pending_presence_body[0] == '\0' &&
            platform_client_ready() && !session_incoming_pending() &&
            session_state() == STARTER_RUNTIME_WAITING) {
            ESP_LOGI(TAG, "restoring suspended Room from cached assignment");
            room_request_token();
        }
        if (!s_room_http_inflight && platform_client_ready() &&
            s_room_sync_due_ms != 0 && current_ms >= s_room_sync_due_ms) {
            room_request_assignment();
        }
        if (s_room_joined && !s_room_http_inflight &&
            current_ms >= s_room_next_heartbeat_ms) {
            room_request_presence("joined");
        }
        if (s_room_joined && s_room_lease_deadline_ms != 0 &&
            current_ms >= s_room_lease_deadline_ms) {
            ESP_LOGW(TAG, "room lease expired locally");
            room_stop_connection("connect_failed", ESP_ERR_TIMEOUT);
        }
        if (s_ai_start_at_ms != 0 && current_ms >= s_ai_start_at_ms) {
            send_ai_start();
        }
        if (s_room_start_at_ms != 0 && current_ms >= s_room_start_at_ms) {
            send_room_join();
        }
        if (!s_voip_profile_ready && !s_voip_profile_inflight &&
            s_voip_profile_retry_at_ms != 0 &&
            current_ms >= s_voip_profile_retry_at_ms) {
            request_device_profile();
        }
        if (starter_media_ai_preroll_failed()) {
            ESP_LOGW(TAG, "AI wake audio discontinuity/overflow/timeout; retry required");
            finish_session(ESP_ERR_TIMEOUT);
        }
        if (session_incoming_pending() && s_session.pending_deadline_ms != 0U &&
            (int32_t)((uint32_t)current_ms - s_session.pending_deadline_ms) >= 0) {
            ESP_LOGW(TAG, "pending incoming call timed out");
            reject_or_hangup_call(true);
        }
        if (s_session.deadline_ms != 0U &&
            (int32_t)((uint32_t)current_ms - s_session.deadline_ms) >= 0) {
            ESP_LOGW(TAG, "session setup timed out");
            starter_runtime_state_t state = session_state();
            if (state == STARTER_RUNTIME_CALL_INCOMING) {
                reject_or_hangup_call(true);
                product_set_call_result("来电已结束");
            } else if (state == STARTER_RUNTIME_CALL_CONNECTING) {
                reject_or_hangup_call(false);
                product_set_call_result("无人接听");
            } else if (state == STARTER_RUNTIME_ROOM_CONNECTING ||
                       state == STARTER_RUNTIME_ROOM_ACTIVE) {
                room_stop_connection("connect_failed", ESP_ERR_TIMEOUT);
            } else {
                finish_session(ESP_ERR_TIMEOUT);
            }
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
    s_product_mutex = xSemaphoreCreateMutex();
    if (s_queue == NULL || s_product_mutex == NULL) {
        return ESP_ERR_NO_MEM;
    }
    product_snapshot_reset();
    /* 必须先注册回调，再由组合根启动 TiRTC，避免丢失早期启动/连接事件。 */
    const starter_tirtc_handlers_t handlers = {
        .on_started = on_tirtc_started,
        .on_connection = on_tirtc_connection,
        .on_command = on_tirtc_command,
        .on_audio = on_tirtc_audio,
#if CONFIG_IDF_TARGET_ESP32P4
        .on_video = on_tirtc_video,
#endif
        .on_key_frame = on_tirtc_key_frame,
    };
    starter_tirtc_set_handlers(&handlers);
    platform_client_set_signal_handler(on_platform_signal, NULL);
    platform_client_set_online_handler(on_platform_online, NULL);
    if (xTaskCreateWithCaps(runtime_task,
                            "starter_session",
                            RUNTIME_TASK_STACK_BYTES,
                            NULL,
                            6,
                            &s_task,
                            MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT) != pdPASS) {
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

esp_err_t starter_runtime_ai_start_from_wake(uint32_t wake_token)
{
    if (wake_token == 0) return ESP_ERR_INVALID_ARG;
    const runtime_event_t event = {.type = EVENT_AI_START, .generation = wake_token};
    return queue_event(&event) ? ESP_OK : ESP_ERR_TIMEOUT;
}

esp_err_t starter_runtime_ai_stop(void)
{
    return enqueue_simple(EVENT_AI_STOP);
}

esp_err_t starter_runtime_main_key(bool double_click)
{
    const runtime_event_t event = {.type = EVENT_MAIN_KEY, .flag = double_click};
    return queue_event(&event) ? ESP_OK : ESP_ERR_TIMEOUT;
}

esp_err_t starter_runtime_contacts_refresh(void)
{
    return enqueue_simple(EVENT_CONTACTS_REFRESH);
}

esp_err_t starter_runtime_wechat_quick_call(void)
{
    return enqueue_simple(EVENT_WECHAT_QUICK_CALL);
}

static bool decimal_string(const char *text, size_t length, bool allow_empty)
{
    if (text == NULL) return allow_empty && length == 0U;
    size_t actual = strlen(text);
    if (actual != length) return allow_empty && actual == 0U;
    for (size_t i = 0; i < actual; ++i) {
        if (!isdigit((unsigned char)text[i])) return false;
    }
    return true;
}

static esp_err_t enqueue_room_json(room_action_t action, const char *json)
{
    runtime_event_t event = {.type = EVENT_ROOM_ACTION, .command = action};
    size_t length = strlen(json);
    if (!copy_event_text(&event, json, length)) return ESP_ERR_NO_MEM;
    if (!queue_event(&event)) { release_event(&event); return ESP_ERR_TIMEOUT; }
    return ESP_OK;
}

esp_err_t starter_runtime_room_set_foreground(bool active)
{
    const runtime_event_t event = {.type = EVENT_ROOM_ACTION,
        .command = ROOM_ACTION_FOREGROUND, .flag = active};
    return queue_event(&event) ? ESP_OK : ESP_ERR_TIMEOUT;
}

esp_err_t starter_runtime_room_refresh(void)
{
    const runtime_event_t event = {.type = EVENT_ROOM_ACTION,
                                   .command = ROOM_ACTION_SYNC};
    return queue_event(&event) ? ESP_OK : ESP_ERR_TIMEOUT;
}

esp_err_t starter_runtime_room_create(const char *password)
{
    if (password == NULL) password = "";
    if (!(password[0] == '\0' || decimal_string(password, 4U, true)))
        return ESP_ERR_INVALID_ARG;
    char json[48];
    (void)snprintf(json, sizeof(json), "{\"password\":\"%s\"}", password);
    return enqueue_room_json(ROOM_ACTION_CREATE, json);
}

esp_err_t starter_runtime_room_join(const char *room_code, const char *password)
{
    if (!decimal_string(room_code, 6U, false)) return ESP_ERR_INVALID_ARG;
    if (password == NULL) password = "";
    if (!(password[0] == '\0' || decimal_string(password, 4U, true)))
        return ESP_ERR_INVALID_ARG;
    char json[96];
    (void)snprintf(json, sizeof(json),
                   "{\"room_code\":\"%s\",\"password\":\"%s\"}",
                   room_code, password);
    return enqueue_room_json(ROOM_ACTION_JOIN, json);
}

esp_err_t starter_runtime_room_leave(void)
{
    const runtime_event_t event = {.type = EVENT_ROOM_ACTION,
                                   .command = ROOM_ACTION_LEAVE};
    return queue_event(&event) ? ESP_OK : ESP_ERR_TIMEOUT;
}

esp_err_t starter_runtime_room_ptt(bool pressed)
{
    /* Release closes the atomic media gate immediately even if the control
     * queue is momentarily full. The state task still owns wire signalling. */
    atomic_store_explicit(&s_room_key_pressed, pressed, memory_order_release);
    if (!pressed) {
        starter_media_set_room_pressed(false);
        atomic_store_explicit(&s_room_release_required, true,
                              memory_order_release);
    }
    const runtime_event_t event = {.type = EVENT_ROOM_ACTION,
                                   .command = ROOM_ACTION_PTT,
                                   .flag = pressed};
    bool queued = queue_event(&event);
    if (pressed) starter_media_set_room_pressed(queued);
    return queued ? ESP_OK : ESP_ERR_TIMEOUT;
}

esp_err_t starter_runtime_call_contact(uint8_t index)
{
    if (index >= STARTER_PRODUCT_CONTACTS_MAX) {
        return ESP_ERR_INVALID_ARG;
    }
    const runtime_event_t event = {
        .type = EVENT_CALL_DIAL,
        .command = index,
    };
    return queue_event(&event) ? ESP_OK : ESP_ERR_TIMEOUT;
}

#if CONFIG_IDF_TARGET_ESP32P4
esp_err_t starter_runtime_call_set_camera_enabled(uint32_t session_generation, bool enabled)
{
    const runtime_event_t event = {.type = EVENT_CALL_CAMERA,
        .generation = session_generation, .flag = enabled};
    return queue_event(&event) ? ESP_OK : ESP_ERR_TIMEOUT;
}

esp_err_t starter_runtime_call_contact_video(uint8_t index)
{
    if (index >= STARTER_PRODUCT_CONTACTS_MAX) return ESP_ERR_INVALID_ARG;
    const runtime_event_t event = {.type = EVENT_CALL_DIAL, .command = index, .flag = true};
    return queue_event(&event) ? ESP_OK : ESP_ERR_TIMEOUT;
}
#endif
esp_err_t starter_runtime_call_accept(void)
{
    return enqueue_simple(EVENT_CALL_ACCEPT);
}

esp_err_t starter_runtime_call_reject(void)
{
    return enqueue_simple(EVENT_CALL_REJECT);
}

esp_err_t starter_runtime_call_hangup(void)
{
    return enqueue_simple(EVENT_CALL_HANGUP);
}

esp_err_t starter_runtime_call_set_microphone_muted(bool muted)
{
    const runtime_event_t event = {
        .type = EVENT_CALL_MIC_MUTE,
        .flag = muted,
    };
    return queue_event(&event) ? ESP_OK : ESP_ERR_TIMEOUT;
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
        .stack_high_water_bytes = s_task == NULL
                                      ? 0U
                                      : (uint32_t)uxTaskGetStackHighWaterMark(s_task),
    };
}

void starter_runtime_copy_device_id(char *out, size_t out_size)
{
    if (out == NULL || out_size == 0U) {
        return;
    }
    (void)snprintf(out, out_size, "%s", s_device_id[0] == '\0' ? "未绑定" : s_device_id);
}

starter_runtime_product_snapshot_t starter_runtime_product_snapshot(void)
{
    starter_runtime_product_snapshot_t snapshot = {0};
    if (s_product_mutex != NULL &&
        xSemaphoreTake(s_product_mutex, pdMS_TO_TICKS(50)) == pdTRUE) {
        snapshot = s_product_snapshot;
        xSemaphoreGive(s_product_mutex);
    }
    return snapshot;
}

const char *starter_runtime_state_name(starter_runtime_state_t state)
{
    switch (state) {
    case STARTER_RUNTIME_WAITING: return "waiting";
    case STARTER_RUNTIME_H5_ACTIVE: return "h5-active";
    case STARTER_RUNTIME_AI_CONNECTING: return "ai-connecting";
    case STARTER_RUNTIME_AI_ACTIVE: return "ai-active";
    case STARTER_RUNTIME_CALL_INCOMING: return "call-incoming";
    case STARTER_RUNTIME_CALL_CONNECTING: return "call-connecting";
    case STARTER_RUNTIME_CALL_ACTIVE: return "call-active";
    case STARTER_RUNTIME_ROOM_CONNECTING: return "room-connecting";
    case STARTER_RUNTIME_ROOM_ACTIVE: return "room-active";
    default: return "unknown";
    }
}
