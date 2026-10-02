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

#include "esp_heap_caps.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "tirtc/tiRTC.h"

#ifndef TIRTC_SDK_STATIC_SEMAPHORE_SIZE
#error "TiRTC SDK build contract did not define StaticSemaphore_t size"
#endif

_Static_assert(sizeof(StaticSemaphore_t) == TIRTC_SDK_STATIC_SEMAPHORE_SIZE,
               "FreeRTOS StaticSemaphore_t does not match the TiRTC SDK build contract");

#define H5_AUDIO_STREAM 10U
#define H5_VIDEO_STREAM 11U
#define AI_AUDIO_STREAM 1U
#define CALL_AUDIO_STREAM 10U
/* SDK permits 1–50 ms. 20 ms avoids idle-core starvation without perceptible
 * call-control latency on this Wi-Fi S3 product. */
#define TIRTC_TGTRP_POLL_TIMEOUT_MS 20

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
static atomic_uint_fast32_t s_downlink_audio_accepted;
static atomic_uint_fast32_t s_downlink_audio_rejected;
static atomic_int s_pending_mode;
static atomic_uint_fast32_t s_expected_call_tag;
static bool s_initialized;

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
    /* 主叫等待 P2P 时，SDK 入站连接属于 CALL；否则才按 H5 处理。 */
    uint32_t call_tag = (uint32_t)atomic_exchange_explicit(
        &s_expected_call_tag, 0, memory_order_acq_rel);
    starter_tirtc_mode_t accepted_mode = call_tag != 0U
                                             ? STARTER_TIRTC_CALL
                                             : STARTER_TIRTC_H5;
    if (accepted_mode == STARTER_TIRTC_H5 &&
        !atomic_load_explicit(&s_accept_h5, memory_order_acquire)) {
        ESP_LOGW(TAG, "H5 connection arrived while a foreground session owns the media path; rejecting");
        (void)TiRtcDisconnect(connection);
        return;
    }
    uintptr_t expected = 0;
    if (!atomic_compare_exchange_strong_explicit(&s_connection,
                                                  &expected,
                                                  (uintptr_t)connection,
                                                  memory_order_acq_rel,
                                                  memory_order_acquire)) {
        ESP_LOGW(TAG, "additional H5 connection rejected");
        (void)TiRtcDisconnect(connection);
        return;
    }
    clear_subscriptions();
    uint32_t generation = next_generation();
    atomic_store_explicit(&s_mode, accepted_mode, memory_order_release);
    atomic_store_explicit(&s_active_generation, generation, memory_order_release);
    ESP_LOGI(TAG, "inbound connection accepted mode=%d generation=%lu",
             (int)accepted_mode, (unsigned long)generation);
    notify_connection(accepted_mode, generation, call_tag, true, 0);
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

static void on_external_connect(int error, tirtc_conn_t connection, void *user_data)
{
    /* request id 已失效说明请求被取消或被新会话取代，成功的迟到连接也要关闭。 */
    uint32_t request = (uint32_t)(uintptr_t)user_data;
    if (request == 0U ||
        request != atomic_load_explicit(&s_pending_request, memory_order_acquire)) {
        if (error == 0 && connection != NULL) {
            (void)TiRtcDisconnect(connection);
        }
        return;
    }
    uint32_t request_tag = (uint32_t)atomic_load_explicit(
        &s_pending_tag, memory_order_acquire);
    starter_tirtc_mode_t pending_mode =
        (starter_tirtc_mode_t)atomic_load_explicit(&s_pending_mode,
                                                    memory_order_acquire);
    atomic_store_explicit(&s_pending_request, 0, memory_order_release);
    if (error != 0 || connection == NULL) {
        notify_connection(pending_mode, 0, request_tag, false, error);
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
        (void)TiRtcDisconnect(connection);
        notify_connection(pending_mode, 0, request_tag, false, TIRTC_E_BUSY);
        return;
    }
    clear_subscriptions();
    uint32_t generation = next_generation();
    atomic_store_explicit(&s_mode, pending_mode, memory_order_release);
    atomic_store_explicit(&s_active_generation, generation, memory_order_release);
    ESP_LOGI(TAG, "external connection ready mode=%d generation=%lu",
             (int)pending_mode, (unsigned long)generation);
    notify_connection(pending_mode, generation, request_tag, true, 0);
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
    /*
     * stream_id 是发送端的媒体流标识，不能把本端的发送流号当成
     * 对端下行流号。设备呼叫和 VoIP 均可能从服务端收到不同的流号。
     * 连接句柄已经完成会话隔离；这里仅验证本产品的可播放编码格式。
     */
    bool expected = mode == STARTER_TIRTC_AI
                        ? frame->media == TIRTC_AUDIO_OPUS &&
                              frame->flags == TIRTC_AUDIOSAMPLE_16K16B1C
                        : frame->media == TIRTC_AUDIO_ALAW &&
                              frame->flags == TIRTC_AUDIOSAMPLE_8K16B1C;
    if (!expected) {
        uint32_t rejected = (uint32_t)atomic_fetch_add_explicit(
                                &s_downlink_audio_rejected, 1,
                                memory_order_relaxed) + 1U;
        if (rejected <= 3U || rejected % 100U == 0U) {
            ESP_LOGW(TAG,
                     "downlink rejected mode=%d stream=%u media=%u flags=%u len=%lu",
                     (int)mode,
                     (unsigned)frame->stream_id,
                     (unsigned)frame->media,
                     (unsigned)frame->flags,
                     (unsigned long)frame->length);
        }
        return;
    }
    starter_tirtc_frame_t copy = {
        .stream_id = frame->stream_id,
        .media = frame->media,
        .flags = frame->flags,
        .timestamp_ms = frame->ts,
        .length = frame->length,
    };
    uint32_t accepted = (uint32_t)atomic_fetch_add_explicit(
                            &s_downlink_audio_accepted, 1,
                            memory_order_relaxed) + 1U;
    if (accepted == 1U || accepted % 100U == 0U) {
        ESP_LOGI(TAG,
                 "downlink accepted mode=%d stream=%u media=%u flags=%u len=%lu",
                 (int)mode,
                 (unsigned)copy.stream_id,
                 (unsigned)copy.media,
                 (unsigned)copy.flags,
                 (unsigned long)copy.length);
    }
    s_handlers.on_audio(mode, generation, &copy, data, s_handlers.user_data);
}

static void on_video(tirtc_conn_t connection,
                     const TIRTCFRAMEINFO *frame,
                     void *data)
{
    if (frame == NULL || data == NULL || !connection_matches(connection)) {
        return;
    }
    if (s_handlers.on_video != NULL) {
        starter_tirtc_frame_t copy = {
            .stream_id = frame->stream_id, .media = frame->media,
            .flags = frame->flags, .timestamp_ms = frame->ts, .length = frame->length,
        };
        s_handlers.on_video(starter_tirtc_mode(), starter_tirtc_generation(),
                             &copy, data, s_handlers.user_data);
    }
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
    /* MJPEG 每帧独立；请求仅用于让采集任务尽快提交下一张完整 JPEG。 */
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
                    (mode == STARTER_TIRTC_AI && stream_id == AI_AUDIO_STREAM) ||
                    ((mode == STARTER_TIRTC_VOIP || mode == STARTER_TIRTC_CALL ||
                      mode == STARTER_TIRTC_ROOM) &&
                     stream_id == CALL_AUDIO_STREAM);
    if (accepted) {
        atomic_store_explicit(&s_audio_subscribed, true, memory_order_release);
    }
    ESP_LOGI(TAG,
             "audio subscribe mode=%d stream=%u accepted=%d",
             (int)mode,
             (unsigned)stream_id,
             accepted);
    return accepted ? 0 : -1;
}

static int on_subscribe_video(tirtc_conn_t connection, uint8_t stream_id)
{
    bool accepted = connection_matches(connection) &&
                    (atomic_load_explicit(&s_mode, memory_order_acquire) == STARTER_TIRTC_H5
#if CONFIG_IDF_TARGET_ESP32P4
                     || starter_tirtc_mode() == STARTER_TIRTC_CALL
                     || starter_tirtc_mode() == STARTER_TIRTC_VOIP
#endif
                    ) &&
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
    /* TiRTC 按 C 字符串拷贝选项值，长度必须包含结尾 NUL；与官方
     * device-monitor 参考实现保持一致，避免 SDK 读取到未终止的地址或凭证。 */
    return TiRtcSetOption(option, value, (uint32_t)strlen(value) + 1U);
}

static void log_start_memory(const char *stage)
{
    ESP_LOGI(TAG,
             "TiRTC %s memory internal-free=%u internal-min=%u internal-largest=%u "
             "dma-free=%u dma-largest=%u psram-free=%u psram-largest=%u",
             stage,
             (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT),
             (unsigned)heap_caps_get_minimum_free_size(MALLOC_CAP_INTERNAL |
                                                       MALLOC_CAP_8BIT),
             (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL |
                                                        MALLOC_CAP_8BIT),
             (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_DMA),
             (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL |
                                                        MALLOC_CAP_DMA),
             (unsigned)heap_caps_get_free_size(MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT),
             (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_SPIRAM |
                                                        MALLOC_CAP_8BIT));
}

static void log_start_failure(const char *stage, int rc)
{
    const char *error = TiRtcGetErrorStr(rc);
    ESP_LOGE(TAG,
             "%s failed rc=%d error=%s internal-free=%u internal-min=%u "
             "internal-largest=%u dma-free=%u dma-largest=%u psram-free=%u "
             "psram-largest=%u",
             stage,
             rc,
             error == NULL ? "unknown" : error,
             (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT),
             (unsigned)heap_caps_get_minimum_free_size(MALLOC_CAP_INTERNAL |
                                                       MALLOC_CAP_8BIT),
             (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL |
                                                        MALLOC_CAP_8BIT),
             (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_DMA),
             (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL |
                                                        MALLOC_CAP_DMA),
             (unsigned)heap_caps_get_free_size(MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT),
             (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_SPIRAM |
                                                        MALLOC_CAP_8BIT));
}

static int rollback_start(const char *stage, int rc)
{
    log_start_failure(stage, rc);
    TiRtcUninit();
    s_initialized = false;
    log_start_memory("post-rollback");
    return rc;
}

int starter_tirtc_start(const starter_tirtc_config_t *config)
{
    if (config == NULL || config->device_id == NULL || config->device_id[0] == '\0' ||
        config->device_secret == NULL || config->device_secret[0] == '\0' ||
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
        if (rc != 0) {
            log_start_failure("TiRtcSetOption(MAX_SEND_BUFFER)", rc);
            return rc;
        }
    }
    log_start_memory("pre-init");
    rc = TiRtcInit();
    if (rc != 0) {
        log_start_failure("TiRtcInit", rc);
        return rc;
    }
    s_initialized = true;
    rc = set_string_option(TIRTC_OPT_DEVICE_SECRET_KEY, config->device_secret);
    if (rc != 0) {
        return rollback_start("TiRtcSetOption(DEVICE_SECRET_KEY)", rc);
    }
    rc = set_string_option(TIRTC_OPT_CLIENT_ID, config->client_id);
    if (rc != 0) {
        return rollback_start("TiRtcSetOption(CLIENT_ID)", rc);
    }
    int network_type = TIRTC_NETCONN_WIFI;
    int max_connections = 1;
    rc = TiRtcSetOption(TIRTC_OPT_NETWORK_TYPE,
                        &network_type,
                        sizeof(network_type));
    if (rc != 0) {
        return rollback_start("TiRtcSetOption(NETWORK_TYPE)", rc);
    }
    rc = TiRtcSetOption(TIRTC_OPT_MAX_CONNECTIONS,
                        &max_connections,
                        sizeof(max_connections));
    if (rc != 0) {
        return rollback_start("TiRtcSetOption(MAX_CONNECTIONS)", rc);
    }
    int tgtrp_poll_timeout_ms = TIRTC_TGTRP_POLL_TIMEOUT_MS;
    rc = TiRtcSetOption(TIRTC_OPT_TGTRP_POLL_TIMEOUT,
                        &tgtrp_poll_timeout_ms,
                        sizeof(tgtrp_poll_timeout_ms));
    if (rc < 0) {
        return rollback_start("TiRtcSetOption(TGTRP_POLL_TIMEOUT)", rc);
    }
    ESP_LOGI(TAG, "TiRTC transport poll timeout=%d ms", tgtrp_poll_timeout_ms);
    log_start_memory("pre-start");
    rc = TiRtcStart(config->device_id, &s_callbacks);
    if (rc != 0) {
        return rollback_start("TiRtcStart", rc);
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

static int external_connect(starter_tirtc_mode_t mode,
                            const char *peer_id,
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
    atomic_store_explicit(&s_pending_mode, mode, memory_order_release);
    int rc = mode == STARTER_TIRTC_CALL
                 ? TiRtcConnect(peer_id, token, on_external_connect,
                                (void *)(uintptr_t)request)
                 : TiRtcWhipConnect(peer_id, token, on_external_connect,
                                    (void *)(uintptr_t)request);
    if (rc != 0) {
        atomic_store_explicit(&s_pending_request, 0, memory_order_release);
    }
    return rc;
}

int starter_tirtc_ai_connect(const char *peer_id,
                             const char *token,
                             uint32_t request_tag)
{
    return external_connect(STARTER_TIRTC_AI, peer_id, token, request_tag);
}

int starter_tirtc_voip_connect(const char *peer_id,
                               const char *token,
                               uint32_t request_tag)
{
    return external_connect(STARTER_TIRTC_VOIP, peer_id, token, request_tag);
}

int starter_tirtc_call_connect(const char *remote_id,
                               const char *token,
                               uint32_t request_tag)
{
    return external_connect(STARTER_TIRTC_CALL, remote_id, token, request_tag);
}

int starter_tirtc_room_connect(const char *peer_id,
                               const char *token,
                               uint32_t request_tag)
{
    return external_connect(STARTER_TIRTC_ROOM, peer_id, token, request_tag);
}

void starter_tirtc_expect_call(uint32_t request_tag)
{
    atomic_store_explicit(&s_expected_call_tag, request_tag, memory_order_release);
}

int starter_tirtc_disconnect(void)
{
    /* 先使待完成回调和当前 handle 失效，再请求 SDK 断开。 */
    atomic_store_explicit(&s_pending_request, 0, memory_order_release);
    atomic_store_explicit(&s_expected_call_tag, 0, memory_order_release);
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

static void on_service_response(const char *body, void *user_data)
{
    (void)body;
    (void)user_data;
}

int starter_tirtc_service_request(const char *path, const char *json_body)
{
    if (path == NULL || path[0] != '/' || json_body == NULL) {
        return TIRTC_E_INVALID_PARAMETER;
    }
    return TiRtcServiceRequest(path,
                               json_body,
                               NULL,
                               on_service_response,
                               NULL);
}

int starter_tirtc_send_alaw(uint32_t timestamp_ms,
                            const void *data,
                            uint32_t length)
{
    tirtc_conn_t connection = (tirtc_conn_t)atomic_load_explicit(
        &s_connection, memory_order_acquire);
    starter_tirtc_mode_t mode = starter_tirtc_mode();
    if (connection == NULL || data == NULL || length == 0U ||
        (mode != STARTER_TIRTC_H5 && mode != STARTER_TIRTC_VOIP &&
         mode != STARTER_TIRTC_CALL &&
         mode != STARTER_TIRTC_ROOM)) {
        return TIRTC_E_INVALID_PARAMETER;
    }
    /* 调用者只提交编码数据；协议 stream/media/flags 在此集中固定。 */
    TIRTCFRAMEINFO frame = {
        .stream_id = CALL_AUDIO_STREAM,
        .media = TIRTC_AUDIO_ALAW,
        .flags = TIRTC_AUDIOSAMPLE_8K16B1C,
        .ts = timestamp_ms,
        .length = length,
    };
    return TiRtcSendAudioStream(connection, &frame, data);
}

int starter_tirtc_send_opus(uint32_t timestamp_ms,
                            const void *data,
                            uint32_t length)
{
    tirtc_conn_t connection = (tirtc_conn_t)atomic_load_explicit(
        &s_connection, memory_order_acquire);
    if (connection == NULL || starter_tirtc_mode() != STARTER_TIRTC_AI ||
        data == NULL || length == 0U) {
        return TIRTC_E_INVALID_PARAMETER;
    }
    TIRTCFRAMEINFO frame = {
        .stream_id = AI_AUDIO_STREAM,
        .media = TIRTC_AUDIO_OPUS,
        .flags = TIRTC_AUDIOSAMPLE_16K16B1C,
        .ts = timestamp_ms,
        .length = length,
    };
    return TiRtcSendAudioStream(connection, &frame, data);
}

int starter_tirtc_send_mjpeg(uint32_t timestamp_ms,
                             const void *data,
                             uint32_t length)
{
    tirtc_conn_t connection = (tirtc_conn_t)atomic_load_explicit(
        &s_connection, memory_order_acquire);
    if (connection == NULL || starter_tirtc_mode() != STARTER_TIRTC_H5 ||
        data == NULL || length == 0U) {
        return TIRTC_E_INVALID_PARAMETER;
    }
    /* 一个 data 缓冲包含一张完整 JPEG；每张图都可独立解码。 */
    TIRTCFRAMEINFO frame = {
        .stream_id = H5_VIDEO_STREAM,
        .media = TIRTC_VIDEO_JPEG,
        .flags = TIRTC_FRAME_FLAG_KEY_FRAME,
        .ts = timestamp_ms,
        .length = length,
    };
    return TiRtcSendVideoStream(connection, &frame, data);
}

bool starter_tirtc_audio_ready(void)
{
    starter_tirtc_mode_t mode = starter_tirtc_mode();
    if (!starter_tirtc_connected()) {
        return false;
    }

    /*
     * H5 是订阅驱动的流，必须等待远端订阅。AI 由运行时业务握手门禁，
     * 设备互呼和微信 VoIP 则在通话确认后才启动 starter_media；SDK 并不
     * 保证会为这两种通话回调 on_subscribe_audio。参考 device-monitor
     * 同样以 call-active 作为设备互呼上行门禁，不能再把本机麦克风卡在
     * 订阅回调上，否则会出现“能听到对方、对方听不到我”。
     */
    return mode == STARTER_TIRTC_AI || mode == STARTER_TIRTC_VOIP ||
           mode == STARTER_TIRTC_CALL || mode == STARTER_TIRTC_ROOM ||
           atomic_load_explicit(&s_audio_subscribed, memory_order_acquire);
}

bool starter_tirtc_video_ready(void)
{
#if CONFIG_IDF_TARGET_ESP32P4
    if (starter_tirtc_connected() && (starter_tirtc_mode() == STARTER_TIRTC_CALL ||
        starter_tirtc_mode() == STARTER_TIRTC_VOIP)) return true;
#endif
    return starter_tirtc_connected() && starter_tirtc_mode() == STARTER_TIRTC_H5 &&
           atomic_load_explicit(&s_video_subscribed, memory_order_acquire);
}

int starter_tirtc_send_h264(uint32_t timestamp_ms, const void *data, uint32_t length, bool key)
{
    tirtc_conn_t conn = (tirtc_conn_t)atomic_load(&s_connection);
    if (!conn || !data || !length || !starter_tirtc_video_ready()) return TIRTC_E_INVALID_PARAMETER;
    TIRTCFRAMEINFO frame = {.stream_id = H5_VIDEO_STREAM, .media = TIRTC_VIDEO_H264,
        .flags = key ? TIRTC_FRAME_FLAG_KEY_FRAME : 0, .ts = timestamp_ms, .length = length};
    return TiRtcSendVideoStream(conn, &frame, data);
}

int starter_tirtc_subscribe_call_video(void)
{
    tirtc_conn_t conn = (tirtc_conn_t)atomic_load(&s_connection);
    if (!conn || (starter_tirtc_mode() != STARTER_TIRTC_CALL &&
                  starter_tirtc_mode() != STARTER_TIRTC_VOIP)) return TIRTC_E_INVALID_PARAMETER;
    int ret = TiRtcSubscribeVideo(conn, H5_VIDEO_STREAM);
#if CONFIG_IDF_TARGET_ESP32P4
    if (ret >= 0 && starter_tirtc_mode() == STARTER_TIRTC_VOIP) {
        /* Pinned monitor tirtc_commands.c: showcase request word is
         * SN<<16 | 0x1105, with one uint8_t enabled payload. A transport
         * subscription alone does not request the WeChat downlink producer. */
        static atomic_uint request_sequence;
        uint32_t sequence = (atomic_fetch_add(&request_sequence, 1) + 1) & 0xffffU;
        const uint8_t enabled = 1;
        ret = TiRtcSendCommand(conn, (sequence << 16) | 0x1105U, &enabled, sizeof(enabled));
    }
#endif
    return ret;
}

int starter_tirtc_request_remote_key_frame(void)
{
    tirtc_conn_t conn = (tirtc_conn_t)atomic_load(&s_connection);
    return conn ? TiRtcRequestKeyFrame(conn, H5_VIDEO_STREAM) : TIRTC_E_INVALID_PARAMETER;
}

size_t starter_tirtc_send_buffer_used(void)
{
    tirtc_conn_t connection = (tirtc_conn_t)atomic_load_explicit(
        &s_connection, memory_order_acquire);
    return connection == NULL ? 0U : TiRtcGetSendBufferUsed(connection);
}
