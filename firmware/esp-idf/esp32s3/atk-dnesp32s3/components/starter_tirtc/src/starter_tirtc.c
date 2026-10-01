/*
 * TiRTC SDK 适配层，也是工程中唯一直接依赖 tiRTC.h 的模块。
 *
 * 模块最多持有一条连接：H5 是 SDK 接受的入站连接，AI 是 WHIP 外连。原子
 * connection/mode/generation 让 SDK 回调与会话任务能识别当前连接，并拒绝
 * 断连后到达的旧回调。SDK 回调只转换参数并通知 starter_runtime/media，
 * 不执行 HTTP、阻塞等待、TiRtcStop 或 TiRtcUninit。
 */
#include "starter_tirtc.h"

#include <stdatomic.h>
#include <stdint.h>
#include <string.h>

#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "tirtc/tiRTC.h"

#ifndef TIRTC_SDK_STATIC_SEMAPHORE_SIZE
#error "TiRTC SDK build contract did not define StaticSemaphore_t size"
#endif

_Static_assert(sizeof(StaticSemaphore_t) == TIRTC_SDK_STATIC_SEMAPHORE_SIZE,
               "FreeRTOS StaticSemaphore_t does not match the TiRTC SDK build contract");

#define H5_AUDIO_STREAM 10U
#define H5_VIDEO_STREAM 11U
#define AI_AUDIO_STREAM 1U
#define DEFERRED_DISCONNECT_DEPTH 4U
#define DISCOVERED_TIRTC_HTTPS "https://ep-tirtc.tange365.com"
#define SDK_TIRTC_HTTP "http://ep-tirtc.tange365.com"

static const char *TAG = "starter_tirtc";

/* 由 starter_runtime 在 SDK 启动前设置；回调表本身在运行期保持稳定。 */
static starter_tirtc_handlers_t s_handlers;

/* SDK 回调和产品任务会并发读取这些字段，因此都使用原子操作。 */
static atomic_bool s_started;
static atomic_bool s_accept_h5 = true;
static atomic_uintptr_t s_connection;
static atomic_int s_mode;

/*
 * generation 标识实际连接；request/request_tag 标识尚未完成的 AI 外连请求。
 * 两层编号分别解决“旧连接回调”和“旧业务请求回调”迟到的问题。
 */
static atomic_uint_fast32_t s_generation_counter;
static atomic_uint_fast32_t s_active_generation;
static atomic_uint_fast32_t s_request_counter;
static atomic_uint_fast32_t s_pending_request;
static atomic_uint_fast32_t s_pending_tag;
static atomic_bool s_audio_subscribed;
static atomic_bool s_video_subscribed;
static bool s_initialized;
static QueueHandle_t s_disconnect_queue;
static TaskHandle_t s_disconnect_task;

/* SDK 生命周期调用只能从产品任务执行，拒绝/迟到连接由本队列移出回调栈。 */
static void deferred_disconnect_task(void *argument)
{
    (void)argument;
    tirtc_conn_t connection;
    for (;;) {
        if (xQueueReceive(s_disconnect_queue, &connection, portMAX_DELAY) == pdTRUE &&
            connection != NULL) {
            (void)TiRtcDisconnect(connection);
        }
    }
}

static bool defer_disconnect(tirtc_conn_t connection)
{
    if (connection == NULL || s_disconnect_queue == NULL ||
        xQueueSend(s_disconnect_queue, &connection, 0) != pdTRUE) {
        ESP_LOGE(TAG, "cannot defer rejected connection cleanup");
        return false;
    }
    return true;
}

static int start_disconnect_worker(void)
{
    if (s_disconnect_queue != NULL) {
        return 0;
    }
    s_disconnect_queue = xQueueCreate(DEFERRED_DISCONNECT_DEPTH,
                                      sizeof(tirtc_conn_t));
    if (s_disconnect_queue == NULL) {
        return TIRTC_E_LACK_OF_RESOURCE;
    }
    if (xTaskCreate(deferred_disconnect_task,
                    "tirtc_cleanup",
                    4096,
                    NULL,
                    7,
                    &s_disconnect_task) != pdPASS) {
        vQueueDelete(s_disconnect_queue);
        s_disconnect_queue = NULL;
        return TIRTC_E_LACK_OF_RESOURCE;
    }
    return 0;
}

static void stop_disconnect_worker(void)
{
    if (s_disconnect_task != NULL) {
        vTaskDelete(s_disconnect_task);
        s_disconnect_task = NULL;
    }
    if (s_disconnect_queue != NULL) {
        vQueueDelete(s_disconnect_queue);
        s_disconnect_queue = NULL;
    }
}

/* SDK 回调携带 opaque handle，只允许当前原子槽位中的 handle 改变状态。 */
static bool connection_matches(tirtc_conn_t connection)
{
    return atomic_load_explicit(&s_connection, memory_order_acquire) ==
           (uintptr_t)connection;
}

static uint32_t next_generation(void)
{
    return (uint32_t)atomic_fetch_add_explicit(
               &s_generation_counter, 1, memory_order_acq_rel) + 1U;
}

static void clear_subscriptions(void)
{
    atomic_store_explicit(&s_audio_subscribed, false, memory_order_release);
    atomic_store_explicit(&s_video_subscribed, false, memory_order_release);
}

static void notify_connection(starter_tirtc_mode_t mode,
                              uint32_t generation,
                              uint32_t request_tag,
                              bool connected,
                              int error)
{
    if (s_handlers.on_connection != NULL) {
        s_handlers.on_connection(mode,
                                 generation,
                                 request_tag,
                                 connected,
                                 error,
                                 s_handlers.user_data);
    }
}

static void sdk_log(const char *log, uint32_t length)
{
    /* SDK 原文可能包含服务地址或握手上下文，模板默认只保留长度。 */
    if (log != NULL && length > 0U) {
        ESP_LOGD(TAG,
                 "SDK diagnostic message hidden (%lu bytes)",
                 (unsigned long)length);
    }
}

static void on_event(int event, const void *data, int length)
{
    /* 系统事件只发布 SDK 生命周期，不在回调中启停其他模块。 */
    (void)data;
    (void)length;
    if (event == TIRTC_EVENT_SYS_STARTED) {
        atomic_store_explicit(&s_started, true, memory_order_release);
        ESP_LOGI(TAG, "SDK started");
        if (s_handlers.on_started != NULL) {
            s_handlers.on_started(true, 0, s_handlers.user_data);
        }
    } else if (event == TIRTC_EVENT_SYS_STOPPED) {
        atomic_store_explicit(&s_started, false, memory_order_release);
        ESP_LOGI(TAG, "SDK stopped");
        if (s_handlers.on_started != NULL) {
            s_handlers.on_started(false, 0, s_handlers.user_data);
        }
    } else if (event == TIRTC_EVENT_ACCESS_HIJACKING) {
        ESP_LOGE(TAG, "SDK reported an unexpected service redirect");
    }
}

static void on_conn_accepted(tirtc_conn_t connection)
{
    /* AI 占用媒体路径时拒绝 H5；其他时候用 CAS 保证只接受第一条入站连接。 */
    if (!atomic_load_explicit(&s_accept_h5, memory_order_acquire)) {
        ESP_LOGW(TAG, "H5 connection arrived while AI owns the media path; rejecting");
        (void)defer_disconnect(connection);
        return;
    }
    uintptr_t expected = 0;
    if (!atomic_compare_exchange_strong_explicit(&s_connection,
                                                  &expected,
                                                  (uintptr_t)connection,
                                                  memory_order_acq_rel,
                                                  memory_order_acquire)) {
        ESP_LOGW(TAG, "additional H5 connection rejected");
        (void)defer_disconnect(connection);
        return;
    }
    clear_subscriptions();
    uint32_t generation = next_generation();
    atomic_store_explicit(&s_mode, STARTER_TIRTC_H5, memory_order_release);
    atomic_store_explicit(&s_active_generation, generation, memory_order_release);
    ESP_LOGI(TAG, "H5 connection accepted generation=%lu", (unsigned long)generation);
    notify_connection(STARTER_TIRTC_H5, generation, 0, true, 0);
}

static void on_conn_error(tirtc_conn_t connection, int error)
{
    if (!connection_matches(connection)) {
        return;
    }
    starter_tirtc_mode_t mode = (starter_tirtc_mode_t)atomic_load_explicit(
        &s_mode, memory_order_acquire);
    uint32_t generation = (uint32_t)atomic_load_explicit(
        &s_active_generation, memory_order_acquire);
    /* 只记录稳定的数值错误码，不把 SDK 原始错误文本直接写入产品日志。 */
    ESP_LOGW(TAG, "connection error=%d", error);
    notify_connection(mode, generation, 0, false, error);
}

static void on_disconnected(tirtc_conn_t connection)
{
    /* CAS 既过滤旧 handle，也保证只有一个回调负责清空当前连接。 */
    uintptr_t expected = (uintptr_t)connection;
    if (!atomic_compare_exchange_strong_explicit(&s_connection,
                                                  &expected,
                                                  0,
                                                  memory_order_acq_rel,
                                                  memory_order_acquire)) {
        return;
    }
    starter_tirtc_mode_t mode = (starter_tirtc_mode_t)atomic_exchange_explicit(
        &s_mode, STARTER_TIRTC_NONE, memory_order_acq_rel);
    uint32_t generation = (uint32_t)atomic_exchange_explicit(
        &s_active_generation, 0, memory_order_acq_rel);
    clear_subscriptions();
    ESP_LOGI(TAG, "connection closed generation=%lu", (unsigned long)generation);
    notify_connection(mode, generation, 0, false, 0);
}

static void on_ai_connect(int error, tirtc_conn_t connection, void *user_data)
{
    /* request id 已失效说明请求被取消或被新会话取代，成功的迟到连接也要关闭。 */
    uint32_t request = (uint32_t)(uintptr_t)user_data;
    if (request == 0U ||
        request != atomic_load_explicit(&s_pending_request, memory_order_acquire)) {
        if (error == 0 && connection != NULL) {
            (void)defer_disconnect(connection);
        }
        return;
    }
    uint32_t request_tag = (uint32_t)atomic_load_explicit(
        &s_pending_tag, memory_order_acquire);
    atomic_store_explicit(&s_pending_request, 0, memory_order_release);
    if (error != 0 || connection == NULL) {
        notify_connection(STARTER_TIRTC_AI, 0, request_tag, false, error);
        return;
    }

    /* H5 可能在 AI 外连期间抢先到达；连接槽位只允许一个赢家。 */
    uintptr_t expected = 0;
    if (!atomic_compare_exchange_strong_explicit(&s_connection,
                                                  &expected,
                                                  (uintptr_t)connection,
                                                  memory_order_acq_rel,
                                                  memory_order_acquire)) {
        ESP_LOGW(TAG, "AI connection completed after another connection won");
        (void)defer_disconnect(connection);
        notify_connection(STARTER_TIRTC_AI, 0, request_tag, false, TIRTC_E_BUSY);
        return;
    }
    clear_subscriptions();
    uint32_t generation = next_generation();
    atomic_store_explicit(&s_mode, STARTER_TIRTC_AI, memory_order_release);
    atomic_store_explicit(&s_active_generation, generation, memory_order_release);
    ESP_LOGI(TAG, "AI connection ready generation=%lu", (unsigned long)generation);
    notify_connection(STARTER_TIRTC_AI, generation, request_tag, true, 0);
}

static void on_audio(tirtc_conn_t connection,
                     const TIRTCFRAMEINFO *frame,
                     void *data)
{
    if (frame == NULL || data == NULL || !connection_matches(connection) ||
        s_handlers.on_audio == NULL) {
        return;
    }
    starter_tirtc_mode_t mode = (starter_tirtc_mode_t)atomic_load_explicit(
        &s_mode, memory_order_acquire);
    uint32_t generation = (uint32_t)atomic_load_explicit(
        &s_active_generation, memory_order_acquire);
    /* 只把协议规定的下行音频流交给媒体模块，其余流在适配层丢弃。 */
    bool expected = (mode == STARTER_TIRTC_H5 && frame->stream_id == 14U) ||
                    (mode == STARTER_TIRTC_AI && frame->stream_id == AI_AUDIO_STREAM);
    if (!expected || frame->media != TIRTC_AUDIO_ALAW ||
        frame->flags != TIRTC_AUDIOSAMPLE_8K16B1C) {
        ESP_LOGW(TAG,
                 "dropping unsupported audio stream=%u media=%u flags=%u",
                 frame->stream_id,
                 frame->media,
                 frame->flags);
        return;
    }
    starter_tirtc_frame_t copy = {
        .stream_id = frame->stream_id,
        .media = frame->media,
        .flags = frame->flags,
        .timestamp_ms = frame->ts,
        .length = frame->length,
    };
    s_handlers.on_audio(mode, generation, &copy, data, s_handlers.user_data);
}

static void on_video(tirtc_conn_t connection,
                     const TIRTCFRAMEINFO *frame,
                     void *data)
{
    if (frame == NULL || data == NULL || !connection_matches(connection)) {
        return;
    }
    /*
     * TODO(product-media-display): 把编码视频帧有界复制到板级解码/显示任务。
     * data 只在当前 SDK 回调内有效；不要在此处解码、刷屏或阻塞等待。
     */
}

static void on_command(tirtc_conn_t connection,
                       uint32_t command,
                       const void *data,
                       uint32_t length)
{
    /* payload 生命周期由上层 handlers 契约处理，本层不保存 SDK 指针。 */
    if (!connection_matches(connection) || s_handlers.on_command == NULL) {
        return;
    }
    starter_tirtc_mode_t mode = (starter_tirtc_mode_t)atomic_load_explicit(
        &s_mode, memory_order_acquire);
    uint32_t generation = (uint32_t)atomic_load_explicit(
        &s_active_generation, memory_order_acquire);
    s_handlers.on_command(mode,
                          generation,
                          command,
                          data,
                          length,
                          s_handlers.user_data);
}

static void on_request_key_frame(tirtc_conn_t connection, uint8_t stream_id)
{
    /* 只有 H5 上行视频 stream 11 的请求会传给产品编码器。 */
    if (connection_matches(connection) && stream_id == H5_VIDEO_STREAM &&
        s_handlers.on_key_frame != NULL) {
        s_handlers.on_key_frame(
            (uint32_t)atomic_load_explicit(&s_active_generation,
                                           memory_order_acquire),
            s_handlers.user_data);
    }
}

static int on_subscribe_audio(tirtc_conn_t connection, uint8_t stream_id)
{
    /* 返回 0 接受协议规定的发送流；其他 stream 明确拒绝。 */
    if (!connection_matches(connection)) {
        return -1;
    }
    starter_tirtc_mode_t mode = (starter_tirtc_mode_t)atomic_load_explicit(
        &s_mode, memory_order_acquire);
    bool accepted = (mode == STARTER_TIRTC_H5 && stream_id == H5_AUDIO_STREAM) ||
                    (mode == STARTER_TIRTC_AI && stream_id == AI_AUDIO_STREAM);
    if (accepted) {
        atomic_store_explicit(&s_audio_subscribed, true, memory_order_release);
    }
    return accepted ? 0 : -1;
}

static int on_subscribe_video(tirtc_conn_t connection, uint8_t stream_id)
{
    bool accepted = connection_matches(connection) &&
                    atomic_load_explicit(&s_mode, memory_order_acquire) ==
                        STARTER_TIRTC_H5 &&
                    stream_id == H5_VIDEO_STREAM;
    if (accepted) {
        atomic_store_explicit(&s_video_subscribed, true, memory_order_release);
        on_request_key_frame(connection, stream_id);
    }
    return accepted ? 0 : -1;
}

static void on_unsubscribe_audio(tirtc_conn_t connection, uint8_t stream_id)
{
    (void)stream_id;
    if (connection_matches(connection)) {
        atomic_store_explicit(&s_audio_subscribed, false, memory_order_release);
    }
}

static void on_unsubscribe_video(tirtc_conn_t connection, uint8_t stream_id)
{
    (void)stream_id;
    if (connection_matches(connection)) {
        atomic_store_explicit(&s_video_subscribed, false, memory_order_release);
    }
}

static const TIRTCCALLBACKS s_callbacks = {
    /* SDK 在自己的线程调用这些函数；每个函数都必须快速返回。 */
    .on_event = on_event,
    .on_conn_accepted = on_conn_accepted,
    .on_conn_error = on_conn_error,
    .on_disconnected = on_disconnected,
    .on_audio = on_audio,
    .on_video = on_video,
    .on_command = on_command,
    .on_request_key_frame = on_request_key_frame,
    .on_subscribe_video = on_subscribe_video,
    .on_unsubscribe_video = on_unsubscribe_video,
    .on_subscribe_audio = on_subscribe_audio,
    .on_unsubscribe_audio = on_unsubscribe_audio,
    .on_sleep_wakeup_info = NULL,
};

const char *starter_tirtc_version(void) { return TiRtcGetVersion(); }
const char *starter_tirtc_build_info(void) { return TiRtcGetBuildInfo(); }

void starter_tirtc_set_handlers(const starter_tirtc_handlers_t *handlers)
{
    if (handlers == NULL) {
        memset(&s_handlers, 0, sizeof(s_handlers));
    } else {
        s_handlers = *handlers;
    }
}

static int set_string_option(TIRTCOPTION option, const char *value)
{
    if (value == NULL || value[0] == '\0') {
        return 0;
    }
    /* SDK 的字符串选项需要包含终止 NUL 的缓冲长度。 */
    return TiRtcSetOption(option, value, (uint32_t)strlen(value) + 1U);
}

static int start_failure(const char *stage, int rc)
{
    ESP_LOGE(TAG, "TiRTC startup stage=%s rc=%d", stage, rc);
    return rc;
}

int starter_tirtc_start(const starter_tirtc_config_t *config)
{
    if (config == NULL || config->device_id == NULL || config->device_id[0] == '\0' ||
        config->device_secret == NULL || config->device_secret[0] == '\0' ||
        config->service_endpoint == NULL || config->service_endpoint[0] == '\0' ||
        s_initialized) {
        return TIRTC_E_INVALID_PARAMETER;
    }
    /*
     * TiRtcInit 前设置全局选项，Init 后设置设备选项，最后只调用一次 Start。
     * 正常业务切换只连接/断开，不反复 Stop/Uninit SDK。
     */
    TiRtcLogSetCallback(sdk_log);
    TiRtcLogSetLevel(config->log_level > 0 ? config->log_level : 3);
    int rc = 0;
    if (config->max_send_buffer_bytes > 0U) {
        rc = TiRtcSetOption(TIRTC_OPT_MAX_SEND_BUFFER,
                            &config->max_send_buffer_bytes,
                            sizeof(config->max_send_buffer_bytes));
        if (rc != 0) return start_failure("max_send_buffer", rc);
    }
    rc = TiRtcInit();
    if (rc != 0) return start_failure("init", rc);
    s_initialized = true;
    const char *sdk_endpoint = config->service_endpoint;
    if (strcmp(sdk_endpoint, DISCOVERED_TIRTC_HTTPS) == 0) {
        sdk_endpoint = SDK_TIRTC_HTTP;
        ESP_LOGW(TAG, "bundled TiRTC SDK requires HTTP for the known service endpoint");
    }
    const char *failed_stage = "disconnect_worker";
    rc = start_disconnect_worker();
    if (rc == 0) {
        failed_stage = "service_endpoint";
        rc = set_string_option(TIRTC_OPT_SERVICE_ENDPOINT, sdk_endpoint);
    }
    if (rc == 0) {
        failed_stage = "device_secret";
        rc = set_string_option(TIRTC_OPT_DEVICE_SECRET_KEY, config->device_secret);
    }
    if (rc == 0) {
        failed_stage = "client_id";
        rc = set_string_option(TIRTC_OPT_CLIENT_ID, config->client_id);
    }
    if (rc != 0) {
        stop_disconnect_worker();
        TiRtcUninit();
        s_initialized = false;
        return start_failure(failed_stage, rc);
    }
    int network_type = TIRTC_NETCONN_WIFI;
    int max_connections = 1;
    failed_stage = "network_type";
    rc = TiRtcSetOption(TIRTC_OPT_NETWORK_TYPE, &network_type, sizeof(network_type));
    if (rc == 0) {
        failed_stage = "max_connections";
        rc = TiRtcSetOption(TIRTC_OPT_MAX_CONNECTIONS,
                            &max_connections,
                            sizeof(max_connections));
    }
    if (rc == 0) {
        failed_stage = "start";
        rc = TiRtcStart(config->device_id, &s_callbacks);
    }
    if (rc != 0) {
        stop_disconnect_worker();
        TiRtcUninit();
        s_initialized = false;
        return start_failure(failed_stage, rc);
    }
    return 0;
}

bool starter_tirtc_started(void)
{
    return atomic_load_explicit(&s_started, memory_order_acquire);
}

void starter_tirtc_accept_h5(bool accept)
{
    atomic_store_explicit(&s_accept_h5, accept, memory_order_release);
}

int starter_tirtc_ai_connect(const char *peer_id,
                             const char *token,
                             uint32_t request_tag)
{
    if (!starter_tirtc_started() || peer_id == NULL || peer_id[0] == '\0' ||
        token == NULL || token[0] == '\0' || starter_tirtc_connected()) {
        return TIRTC_E_INVALID_PARAMETER;
    }
    /* pending_request 只允许一条尚未完成的 WHIP 请求。 */
    uint32_t request = (uint32_t)atomic_fetch_add_explicit(
                           &s_request_counter, 1, memory_order_acq_rel) + 1U;
    uint32_t expected = 0;
    if (!atomic_compare_exchange_strong_explicit(&s_pending_request,
                                                  &expected,
                                                  request,
                                                  memory_order_acq_rel,
                                                  memory_order_acquire)) {
        return TIRTC_E_BUSY;
    }
    atomic_store_explicit(&s_pending_tag, request_tag, memory_order_release);
    int rc = TiRtcWhipConnect(peer_id,
                              token,
                              on_ai_connect,
                              (void *)(uintptr_t)request);
    if (rc != 0) {
        atomic_store_explicit(&s_pending_request, 0, memory_order_release);
    }
    return rc;
}

int starter_tirtc_disconnect(void)
{
    /* 先使待完成回调和当前 handle 失效，再请求 SDK 断开。 */
    atomic_store_explicit(&s_pending_request, 0, memory_order_release);
    tirtc_conn_t connection = (tirtc_conn_t)atomic_exchange_explicit(
        &s_connection, 0, memory_order_acq_rel);
    atomic_store_explicit(&s_mode, STARTER_TIRTC_NONE, memory_order_release);
    atomic_store_explicit(&s_active_generation, 0, memory_order_release);
    clear_subscriptions();
    return connection == NULL ? TIRTC_E_INVALID_HANDLE : TiRtcDisconnect(connection);
}

bool starter_tirtc_connected(void)
{
    return atomic_load_explicit(&s_connection, memory_order_acquire) != 0;
}

starter_tirtc_mode_t starter_tirtc_mode(void)
{
    return (starter_tirtc_mode_t)atomic_load_explicit(&s_mode,
                                                       memory_order_acquire);
}

uint32_t starter_tirtc_generation(void)
{
    return (uint32_t)atomic_load_explicit(&s_active_generation,
                                          memory_order_acquire);
}

int starter_tirtc_send_command(uint32_t command,
                               const void *data,
                               uint32_t length)
{
    tirtc_conn_t connection = (tirtc_conn_t)atomic_load_explicit(
        &s_connection, memory_order_acquire);
    return connection == NULL
               ? TIRTC_E_INVALID_HANDLE
               : TiRtcSendCommand(connection, command, data, length);
}

int starter_tirtc_send_alaw(uint32_t timestamp_ms,
                            const void *data,
                            uint32_t length)
{
    tirtc_conn_t connection = (tirtc_conn_t)atomic_load_explicit(
        &s_connection, memory_order_acquire);
    starter_tirtc_mode_t mode = starter_tirtc_mode();
    if (connection == NULL || data == NULL || length == 0U ||
        (mode != STARTER_TIRTC_H5 && mode != STARTER_TIRTC_AI)) {
        return TIRTC_E_INVALID_PARAMETER;
    }
    /* 调用者只提交编码数据；协议 stream/media/flags 在此集中固定。 */
    TIRTCFRAMEINFO frame = {
        .stream_id = mode == STARTER_TIRTC_H5 ? H5_AUDIO_STREAM : AI_AUDIO_STREAM,
        .media = TIRTC_AUDIO_ALAW,
        .flags = TIRTC_AUDIOSAMPLE_8K16B1C,
        .ts = timestamp_ms,
        .length = length,
    };
    return TiRtcSendAudioStream(connection, &frame, data);
}

int starter_tirtc_send_video(starter_video_codec_t codec,
                             uint32_t timestamp_ms,
                             bool key_frame,
                             const void *data,
                             uint32_t length)
{
    tirtc_conn_t connection = (tirtc_conn_t)atomic_load_explicit(
        &s_connection, memory_order_acquire);
    if (connection == NULL || starter_tirtc_mode() != STARTER_TIRTC_H5 ||
        data == NULL || length == 0U) {
        return TIRTC_E_INVALID_PARAMETER;
    }
    uint8_t media;
    switch (codec) {
    case STARTER_VIDEO_MJPEG: media = TIRTC_VIDEO_JPEG; break;
    case STARTER_VIDEO_H264: media = TIRTC_VIDEO_H264; break;
    case STARTER_VIDEO_H265: media = TIRTC_VIDEO_H265; break;
    default: return TIRTC_E_INVALID_PARAMETER;
    }
    /* data 必须是一个完整 JPEG 或一个完整 Annex-B access unit。 */
    TIRTCFRAMEINFO frame = {
        .stream_id = H5_VIDEO_STREAM,
        .media = media,
        .flags = key_frame ? TIRTC_FRAME_FLAG_KEY_FRAME : 0,
        .ts = timestamp_ms,
        .length = length,
    };
    return TiRtcSendVideoStream(connection, &frame, data);
}

int starter_tirtc_send_jpeg(uint32_t timestamp_ms,
                            const void *data,
                            uint32_t length)
{
    return starter_tirtc_send_video(STARTER_VIDEO_MJPEG,
                                    timestamp_ms,
                                    true,
                                    data,
                                    length);
}

int starter_tirtc_send_h264(uint32_t timestamp_ms,
                            bool key_frame,
                            const void *data,
                            uint32_t length)
{
    return starter_tirtc_send_video(STARTER_VIDEO_H264,
                                    timestamp_ms,
                                    key_frame,
                                    data,
                                    length);
}

int starter_tirtc_send_h265(uint32_t timestamp_ms,
                            bool key_frame,
                            const void *data,
                            uint32_t length)
{
    return starter_tirtc_send_video(STARTER_VIDEO_H265,
                                    timestamp_ms,
                                    key_frame,
                                    data,
                                    length);
}

bool starter_tirtc_audio_ready(void)
{
    /* H5 必须等远端订阅；AI 的业务握手门禁由 starter_runtime 控制。 */
    return starter_tirtc_connected() &&
           (starter_tirtc_mode() == STARTER_TIRTC_AI ||
            atomic_load_explicit(&s_audio_subscribed, memory_order_acquire));
}

bool starter_tirtc_video_ready(void)
{
    return starter_tirtc_connected() && starter_tirtc_mode() == STARTER_TIRTC_H5 &&
           atomic_load_explicit(&s_video_subscribed, memory_order_acquire);
}

size_t starter_tirtc_send_buffer_used(void)
{
    tirtc_conn_t connection = (tirtc_conn_t)atomic_load_explicit(
        &s_connection, memory_order_acquire);
    return connection == NULL ? 0U : TiRtcGetSendBufferUsed(connection);
}
