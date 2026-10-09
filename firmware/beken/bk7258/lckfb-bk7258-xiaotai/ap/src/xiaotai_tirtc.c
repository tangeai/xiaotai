#include "xiaotai_tirtc.h"

#include "xiaotai_log.h"
#include <os/mem.h>
#include <os/os.h>
#include <stdatomic.h>
#include <stdio.h>
#include <string.h>

#include "cJSON.h"
#include "tirtc/tiRTC.h"
#include "xiaotai_audio.h"
#include "xiaotai_tirtc_recovery.h"
#include "xiaotai_video.h"

#define TAG "xiaotai_tirtc"
#define TIRTC_LOG_LEVEL_MAX 17
#define TIRTC_SDK_LOG_CHUNK_LEN 255U

static volatile xiaotai_tirtc_state_t s_state = XIAOTAI_TIRTC_STOPPED;
static bool s_initialized;
/* Preserve the SDK's own default until the console explicitly overrides it.
 * Values above 10 select tgtrp level (value - 10); 17 enables level 7. */
static int s_log_level = XIAOTAI_TIRTC_LOG_LEVEL_SDK_DEFAULT;
static xiaotai_tirtc_handlers_t s_handlers;

/* TiRTC passes a byte span, not a NUL-terminated string.  Copy bounded
 * chunks before sending them to Beken's raw console logger so SDK messages
 * reach the UART unchanged and never become a printf format string.  Keep
 * this callback outside the XiaoTai log wrapper: the SDK may invoke it from
 * its transport threads and routing it back through a tagged logger can
 * recurse or add enough buffering to hide the ICE diagnostics we need. */
static void tirtc_sdk_log_callback(const char *log, uint32_t length)
{
    if (log == NULL || length == 0U) return;

    while (length > 0U) {
        uint32_t chunk_length = length;
        if (chunk_length > TIRTC_SDK_LOG_CHUNK_LEN) {
            chunk_length = TIRTC_SDK_LOG_CHUNK_LEN;
        }

        char chunk[TIRTC_SDK_LOG_CHUNK_LEN + 1U];
        memcpy(chunk, log, chunk_length);
        chunk[chunk_length] = '\0';
        BK_LOG_RAW("%s", chunk);

        log += chunk_length;
        length -= chunk_length;
    }
}

#define STREAM_UP_AUDIO_ID 10U
#define STREAM_UP_VIDEO_ID 11U
#define STREAM_DOWN_AUDIO_ID 14U
#define VOIP_AUDIO_ID 0U
#define AI_AUDIO_ID 1U
#define ROOM_AUDIO_ID 1U
#define MEDIA_QUEUE_DEPTH 8U
#define OUTGOING_CONNECT_MIN_HEAP (64U * 1024U)
#define OUTGOING_CLEANUP_GRACE_MS 3000U
#define OUTGOING_RETAINED_HEAP_LIMIT (12U * 1024U)
#define AI_CLOSE_PLAYBACK_QUIET_MS 120U
#define AI_CLOSE_DRAIN_TIMEOUT_MS 3000U
#define AI_CLOSE_DRAIN_POLL_MS 10U

typedef enum {
    CONNECTION_NONE = 0,
    CONNECTION_STREAM,
    CONNECTION_AI,
    CONNECTION_CALL,
    CONNECTION_VOIP,
    CONNECTION_ROOM,
} connection_mode_t;

typedef enum {
    MEDIA_EVENT_CONNECTED = 1,
    MEDIA_EVENT_DISCONNECT,
    MEDIA_EVENT_CLOSED,
    MEDIA_EVENT_VIDEO_START,
    MEDIA_EVENT_VIDEO_STOP,
    MEDIA_EVENT_KEYFRAME,
    MEDIA_EVENT_AI_CONNECTED,
    MEDIA_EVENT_AI_COMMAND,
    MEDIA_EVENT_CALL_CONNECTED,
    MEDIA_EVENT_CALL_ACCEPT,
    MEDIA_EVENT_VOIP_CONNECTED,
    MEDIA_EVENT_VOIP_ACCEPT,
    MEDIA_EVENT_ROOM_CONNECTED,
    MEDIA_EVENT_ROOM_COMMAND,
} media_event_type_t;

typedef struct {
    media_event_type_t type;
    tirtc_conn_t connection;
    uint32_t generation;
    int error;
    connection_mode_t mode;
    uint32_t command;
    void *data;
    uint32_t length;
} media_event_t;

static beken_queue_t s_media_queue;
static beken_thread_t s_media_thread;
static atomic_uintptr_t s_connection;
static atomic_uint_fast32_t s_generation_counter;
static atomic_uint_fast32_t s_active_generation;
static atomic_int s_connection_error;
static atomic_int s_connection_mode;
static atomic_uint_fast32_t s_pending_ai_generation;
static atomic_uint_fast32_t s_pending_call_generation;
static atomic_uint_fast32_t s_pending_outbound_call_generation;
static atomic_bool s_call_is_caller;
static atomic_bool s_call_accept_seen;
static atomic_uint_fast32_t s_pending_voip_generation;
static atomic_uint_fast32_t s_pending_room_generation;
/* Remote hangup and the SDK error callback can arrive in the same transport
 * turn.  Only one owner may ask the SDK to destroy the active connection. */
static atomic_bool s_active_disconnect_queued;
/* Keep ownership until a failed/stale handle emits on_disconnected(). The
 * SDK can still own DTLS and PeerConnection memory after a connect result. */
static atomic_bool s_outgoing_connect_inflight;
static atomic_uintptr_t s_outgoing_cleanup_connection;
static atomic_bool s_recovery_required;
static atomic_bool s_cleanup_probe_active;
static atomic_uint_fast32_t s_cleanup_probe_started_ms;
static atomic_uint_fast32_t s_outgoing_heap_baseline;
static atomic_bool s_voip_accept_seen;
static atomic_bool s_audio_subscribed;
static atomic_bool s_video_subscribed;
static atomic_uint_fast32_t s_rejected_audio_frames;
static atomic_uint_fast32_t s_received_audio_frames;
static uint32_t s_echo_diag_generation;
static connection_mode_t s_echo_diag_mode;
static uint32_t s_echo_diag_packets;
static uint32_t s_echo_diag_bytes;
static uint32_t s_echo_diag_unexpected_streams;
static uint32_t s_echo_diag_last_rx_ms;
static uint32_t s_echo_diag_gap_min_ms;
static uint32_t s_echo_diag_gap_max_ms;
static uint64_t s_echo_diag_gap_sum_ms;
static char s_active_call_room[128];

static bool connection_matches(tirtc_conn_t connection)
{
    return atomic_load_explicit(&s_connection, memory_order_acquire) ==
           (uintptr_t)connection;
}

/* TiRTC media/command APIs return a non-negative accepted byte/count value,
 * not necessarily zero.  Keep the SDK value at the boundary only long enough
 * to detect errors, then expose the BK-style zero-success contract internally. */
static int normalize_tirtc_result(int rc)
{
    return rc < 0 ? rc : BK_OK;
}

static void drain_ai_playback_after_transport_close(void)
{
    xiaotai_audio_log_playback_status("transport-close");
    uint32_t started_ms = rtos_get_time();
    while (xiaotai_audio_running() &&
           !xiaotai_audio_playback_is_drained(AI_CLOSE_PLAYBACK_QUIET_MS) &&
           (int32_t)(rtos_get_time() - started_ms) <
               (int32_t)AI_CLOSE_DRAIN_TIMEOUT_MS) {
        rtos_delay_milliseconds(AI_CLOSE_DRAIN_POLL_MS);
    }
    bool drained = xiaotai_audio_playback_is_drained(
        AI_CLOSE_PLAYBACK_QUIET_MS);
    xiaotai_audio_log_playback_status(drained ? "drained" : "timeout");
    BK_LOGI(TAG, "AI playback after transport close %s elapsed=%u ms\n",
            drained ? "drained" : "timeout",
            (unsigned)(rtos_get_time() - started_ms));
}

static const char *connection_mode_name(connection_mode_t mode)
{
    switch (mode) {
        case CONNECTION_STREAM: return "stream";
        case CONNECTION_AI: return "ai";
        case CONNECTION_CALL: return "call";
        case CONNECTION_VOIP: return "voip";
        case CONNECTION_ROOM: return "room";
        default: return "none";
    }
}

static uint8_t expected_downlink_audio_stream(connection_mode_t mode)
{
    switch (mode) {
        case CONNECTION_STREAM: return STREAM_DOWN_AUDIO_ID;
        case CONNECTION_AI: return AI_AUDIO_ID;
        case CONNECTION_VOIP: return VOIP_AUDIO_ID;
        case CONNECTION_ROOM: return ROOM_AUDIO_ID;
        case CONNECTION_CALL: return STREAM_UP_AUDIO_ID;
        default: return UINT8_MAX;
    }
}

static void echo_diag_audio_rx(connection_mode_t mode,
                               const TIRTCFRAMEINFO *frame)
{
    uint32_t generation = (uint32_t)atomic_load_explicit(
        &s_active_generation, memory_order_acquire);
    if (s_echo_diag_generation != generation || s_echo_diag_mode != mode) {
        s_echo_diag_generation = generation;
        s_echo_diag_mode = mode;
        s_echo_diag_packets = 0U;
        s_echo_diag_bytes = 0U;
        s_echo_diag_unexpected_streams = 0U;
        s_echo_diag_last_rx_ms = 0U;
        s_echo_diag_gap_min_ms = UINT32_MAX;
        s_echo_diag_gap_max_ms = 0U;
        s_echo_diag_gap_sum_ms = 0U;
    }

    uint32_t now_ms = rtos_get_time();
    uint32_t gap_ms = 0U;
    if (s_echo_diag_last_rx_ms != 0U) {
        gap_ms = now_ms - s_echo_diag_last_rx_ms;
        if (gap_ms < s_echo_diag_gap_min_ms) s_echo_diag_gap_min_ms = gap_ms;
        if (gap_ms > s_echo_diag_gap_max_ms) s_echo_diag_gap_max_ms = gap_ms;
        s_echo_diag_gap_sum_ms += gap_ms;
    }
    s_echo_diag_last_rx_ms = now_ms;
    ++s_echo_diag_packets;
    s_echo_diag_bytes += frame->length;

    uint8_t expected = expected_downlink_audio_stream(mode);
    bool unexpected = frame->stream_id != expected;
    if (unexpected) ++s_echo_diag_unexpected_streams;
    if (s_echo_diag_packets == 1U ||
        (unexpected && (s_echo_diag_unexpected_streams <= 3U ||
                        (s_echo_diag_unexpected_streams % 100U) == 0U))) {
        BK_LOGW(TAG,
                "ECHO_DIAG RX mode=%s generation=%u packet=%u stream=%u "
                "expected=%u bytes=%u gap-ms=%u unexpected=%u\n",
                connection_mode_name(mode), (unsigned)generation,
                (unsigned)s_echo_diag_packets, frame->stream_id, expected,
                (unsigned)frame->length, (unsigned)gap_ms,
                (unsigned)s_echo_diag_unexpected_streams);
    }
    if ((s_echo_diag_packets % 100U) == 0U) {
        uint32_t gap_count = s_echo_diag_packets - 1U;
        uint32_t gap_mean = gap_count == 0U ? 0U :
            (uint32_t)(s_echo_diag_gap_sum_ms / gap_count);
        uint32_t gap_min = s_echo_diag_gap_min_ms == UINT32_MAX ? 0U :
                           s_echo_diag_gap_min_ms;
        BK_LOGI(TAG,
                "ECHO_DIAG RX-SUM mode=%s generation=%u packets=%u bytes=%u "
                "stream=%u expected=%u unexpected=%u gap-ms=%u/%u/%u\n",
                connection_mode_name(mode), (unsigned)generation,
                (unsigned)s_echo_diag_packets, (unsigned)s_echo_diag_bytes,
                frame->stream_id, expected,
                (unsigned)s_echo_diag_unexpected_streams,
                (unsigned)gap_min, (unsigned)gap_mean,
                (unsigned)s_echo_diag_gap_max_ms);
    }
}

static int subscribe_stream_talkback_audio(tirtc_conn_t connection)
{
    int rc = TiRtcSubscribeAudio(connection, STREAM_DOWN_AUDIO_ID);
    if (rc >= 0) {
        BK_LOGI(TAG,
                "H5 talkback audio subscribed stream=%u\n",
                STREAM_DOWN_AUDIO_ID);
    }
    return normalize_tirtc_result(rc);
}

static bool queue_media_event(media_event_type_t type,
                              tirtc_conn_t connection,
                              uint32_t generation,
                              int error)
{
    media_event_t event = {
        .type = type,
        .connection = connection,
        .generation = generation,
        .error = error,
        .mode = (connection_mode_t)atomic_load_explicit(
            &s_connection_mode, memory_order_acquire),
    };
    if (s_media_queue == NULL ||
        rtos_push_to_queue(&s_media_queue, &event, 0) != BK_OK) {
        BK_LOGE(TAG, "media event queue full type=%d\n", (int)type);
        return false;
    }
    return true;
}

static bool queue_active_disconnect_once(tirtc_conn_t connection,
                                         uint32_t generation,
                                         int error)
{
    if (connection == NULL || !connection_matches(connection)) return false;
    bool expected = false;
    if (!atomic_compare_exchange_strong_explicit(
            &s_active_disconnect_queued, &expected, true,
            memory_order_acq_rel, memory_order_acquire)) {
        BK_LOGI(TAG,
                "duplicate disconnect ignored handle=%p generation=%u error=%d\n",
                connection, (unsigned)generation, error);
        return true;
    }
    if (!queue_media_event(MEDIA_EVENT_DISCONNECT, connection, generation,
                           error)) {
        atomic_store_explicit(&s_active_disconnect_queued, false,
                              memory_order_release);
        return false;
    }
    return true;
}

static bool claim_outgoing_connect(void)
{
    uint32_t free_heap = rtos_get_free_heap_size();
    if (free_heap < OUTGOING_CONNECT_MIN_HEAP) {
        atomic_store_explicit(&s_recovery_required, true,
                              memory_order_release);
        BK_LOGE(TAG,
                "outgoing connect blocked: heap=%u requires=%u; recovery scheduled\n",
                (unsigned)free_heap,
                (unsigned)OUTGOING_CONNECT_MIN_HEAP);
        return false;
    }
    bool expected = false;
    bool claimed = atomic_compare_exchange_strong_explicit(
        &s_outgoing_connect_inflight, &expected, true,
        memory_order_acq_rel, memory_order_acquire);
    if (claimed) {
        atomic_store_explicit(&s_outgoing_heap_baseline, free_heap,
                              memory_order_release);
    }
    return claimed;
}

static void release_outgoing_connect(void)
{
    atomic_store_explicit(&s_outgoing_connect_inflight, false,
                          memory_order_release);
}

static void require_recovery_if_heap_low(const char *reason)
{
    uint32_t free_heap = rtos_get_free_heap_size();
    if (free_heap >= OUTGOING_CONNECT_MIN_HEAP) return;
    atomic_store_explicit(&s_recovery_required, true, memory_order_release);
    BK_LOGE(TAG, "TiRTC recovery required reason=%s heap=%u requires=%u\n",
            reason, (unsigned)free_heap,
            (unsigned)OUTGOING_CONNECT_MIN_HEAP);
}

static void release_outgoing_without_handle(const char *reason)
{
    uint32_t free_heap = rtos_get_free_heap_size();
    uint32_t baseline = (uint32_t)atomic_load_explicit(
        &s_outgoing_heap_baseline, memory_order_acquire);
    if (free_heap < OUTGOING_CONNECT_MIN_HEAP) {
        atomic_store_explicit(&s_recovery_required, true,
                              memory_order_release);
        BK_LOGE(TAG,
                "TiRTC recovery required reason=%s heap=%u baseline=%u\n",
                reason, (unsigned)free_heap, (unsigned)baseline);
        return;
    }
    /* A NULL callback handle gives the application nothing it can drain.
     * Keep the adapter busy while the SDK gets a bounded opportunity to
     * destroy its WHIP/DTLS worker. Recovery polling compares against the
     * pre-connect heap before allowing another outgoing session. */
    atomic_store_explicit(&s_cleanup_probe_started_ms, rtos_get_time(),
                          memory_order_release);
    atomic_store_explicit(&s_cleanup_probe_active, true,
                          memory_order_release);
    BK_LOGW(TAG,
            "waiting for TiRTC null-handle cleanup reason=%s heap=%u baseline=%u grace=%u\n",
            reason, (unsigned)free_heap, (unsigned)baseline,
            (unsigned)OUTGOING_CLEANUP_GRACE_MS);
}

static bool queue_outgoing_cleanup(tirtc_conn_t connection,
                                   uint32_t generation,
                                   const char *reason)
{
    if (connection == NULL) return false;
    uintptr_t expected = 0;
    if (!atomic_compare_exchange_strong_explicit(
            &s_outgoing_cleanup_connection, &expected,
            (uintptr_t)connection, memory_order_acq_rel,
            memory_order_acquire)) {
        BK_LOGE(TAG,
                "outgoing cleanup collision handle=%p owned=%p reason=%s\n",
                connection, (void *)expected, reason);
        return false;
    }
    if (!queue_media_event(MEDIA_EVENT_DISCONNECT, connection, generation,
                           TIRTC_E_CONN_OTHER_ERROR)) {
        BK_LOGE(TAG,
                "outgoing cleanup queue failed handle=%p reason=%s\n",
                connection, reason);
        return false;
    }
    BK_LOGI(TAG, "outgoing cleanup queued handle=%p reason=%s\n",
            connection, reason);
    return true;
}

static int send_encoded_audio(const uint8_t *audio,
                              size_t size,
                              uint32_t timestamp_ms,
                              void *context)
{
    (void)context;
    tirtc_conn_t connection = (tirtc_conn_t)atomic_load_explicit(
        &s_connection, memory_order_acquire);
    connection_mode_t mode = (connection_mode_t)atomic_load_explicit(
        &s_connection_mode, memory_order_acquire);
    /* H5 and AI publishing are subscription-driven. Device calls and WeChat
     * VoIP start capture after call acceptance; group-room audio is driven by
     * the local PTT state. None of those paths may wait for an optional
     * on_subscribe_audio callback before publishing. */
    bool subscription_required = mode == CONNECTION_STREAM ||
                                 mode == CONNECTION_AI;
    if (connection == NULL ||
        atomic_load_explicit(&s_active_disconnect_queued,
                             memory_order_acquire) ||
        (subscription_required &&
         !atomic_load_explicit(&s_audio_subscribed,
                               memory_order_acquire))) {
        return TIRTC_E_BUSY;
    }
    TIRTCFRAMEINFO frame = {
        .stream_id = mode == CONNECTION_AI ? AI_AUDIO_ID :
                     (mode == CONNECTION_ROOM ? ROOM_AUDIO_ID :
                      (mode == CONNECTION_VOIP ? VOIP_AUDIO_ID :
                                                STREAM_UP_AUDIO_ID)),
        .media = mode == CONNECTION_AI ? TIRTC_AUDIO_OPUS :
                                         TIRTC_AUDIO_ALAW,
        .flags = mode == CONNECTION_AI ? TIRTC_AUDIOSAMPLE_16K16B1C :
                                         TIRTC_AUDIOSAMPLE_8K16B1C,
        .ts = timestamp_ms,
        .length = (uint32_t)size,
    };
    return TiRtcSendAudioStream(connection, &frame, audio);
}

static int send_h264(const uint8_t *annexb,
                     size_t size,
                     uint32_t timestamp_ms,
                     bool key_frame,
                     void *context)
{
    (void)context;
    tirtc_conn_t connection = (tirtc_conn_t)atomic_load_explicit(
        &s_connection, memory_order_acquire);
    if (connection == NULL ||
        !atomic_load_explicit(&s_video_subscribed, memory_order_acquire)) {
        return TIRTC_E_BUSY;
    }
    TIRTCFRAMEINFO frame = {
        .stream_id = STREAM_UP_VIDEO_ID,
        .media = TIRTC_VIDEO_H264,
        .flags = key_frame ? TIRTC_FRAME_FLAG_KEY_FRAME : 0,
        .ts = timestamp_ms,
        .length = (uint32_t)size,
    };
    return TiRtcSendVideoStream(connection, &frame, annexb);
}

static void media_worker(beken_thread_arg_t argument)
{
    (void)argument;
    media_event_t event;
    for (;;) {
        if (rtos_pop_from_queue(&s_media_queue, &event,
                                BEKEN_WAIT_FOREVER) != BK_OK) {
            continue;
        }
        if (event.type == MEDIA_EVENT_DISCONNECT) {
            if (event.connection != NULL) {
                /* The SDK connection must outlive every media callback. Stop
                 * and join capture first so xiaotai_mic cannot enter
                 * TiRtcSendAudioStream while TiRtcDisconnect frees its
                 * channel/KCP state. Stale outgoing cleanup does not own the
                 * active media pipeline and must not stop it. */
                if (connection_matches(event.connection)) {
                    (void)xiaotai_video_stop();
                    /* A remote AI close can race end_session while decoded
                     * speech remains queued. Drain before audio_stop clears
                     * that queue; user hangup still stops immediately. */
                    if (event.mode == CONNECTION_AI && event.error != 0) {
                        xiaotai_audio_set_uplink_enabled(false);
                        drain_ai_playback_after_transport_close();
                    }
                    (void)xiaotai_audio_stop();
                }
                BK_LOGI(TAG,
                        "TiRtcDisconnect begin handle=%p mode=%d generation=%u\n",
                        event.connection, (int)event.mode,
                        (unsigned)event.generation);
                (void)TiRtcDisconnect(event.connection);
                BK_LOGI(TAG, "TiRtcDisconnect returned handle=%p\n",
                        event.connection);
            }
            continue;
        }
        if (event.type == MEDIA_EVENT_CONNECTED) {
            if (!connection_matches(event.connection)) continue;
            xiaotai_audio_set_uplink_enabled(false);
            bool accepted = s_handlers.on_stream_connected == NULL ||
                s_handlers.on_stream_connected(event.generation,
                                                s_handlers.context);
            int rc = accepted ?
                xiaotai_audio_start(XIAOTAI_AUDIO_CODEC_ALAW_8K,
                                    send_encoded_audio, NULL) :
                TIRTC_E_BUSY;
            if (rc == BK_OK) {
                rc = subscribe_stream_talkback_audio(event.connection);
            }
            if (rc < 0) {
                BK_LOGE(TAG, "stream media start failed rc=%d\n", rc);
                (void)xiaotai_audio_stop();
                (void)queue_active_disconnect_once(
                    event.connection, event.generation, rc);
            } else {
                BK_LOGI(TAG, "STREAM audio active generation=%u\n",
                        (unsigned)event.generation);
            }
            continue;
        }
        if (event.type == MEDIA_EVENT_AI_CONNECTED) {
            if (connection_matches(event.connection) &&
                s_handlers.on_ai_connected != NULL) {
                s_handlers.on_ai_connected(event.generation,
                                           s_handlers.context);
            }
            continue;
        }
        if (event.type == MEDIA_EVENT_CALL_CONNECTED) {
            if (!connection_matches(event.connection)) continue;
            xiaotai_audio_set_uplink_enabled(true); /* call-active */
            int rc = xiaotai_audio_start(XIAOTAI_AUDIO_CODEC_ALAW_8K,
                                         send_encoded_audio, NULL);
            if (rc == BK_OK) {
                rc = normalize_tirtc_result(
                    TiRtcSubscribeAudio(event.connection, STREAM_UP_AUDIO_ID));
            }
            char command[176];
            int length = snprintf(command, sizeof(command),
                                  "{\"room_id\":\"%s\"}",
                                  s_active_call_room);
            if (rc == BK_OK) {
                rc = length > 0 && (size_t)length < sizeof(command) ?
                    TiRtcSendCommand(event.connection, 0x2000U, command,
                                     (uint32_t)length) :
                    TIRTC_E_INVALID_PARAMETER;
            }
            if (rc < 0) {
                BK_LOGE(TAG, "call media start failed rc=%d\n", rc);
                (void)xiaotai_audio_stop();
                (void)queue_active_disconnect_once(
                    event.connection, event.generation, rc);
            } else if (s_handlers.on_call_connected != NULL) {
                BK_LOGI(TAG,
                        "device-call uplink active role=callee stream=%u subscription-gate=0\n",
                        STREAM_UP_AUDIO_ID);
                s_handlers.on_call_connected(event.generation,
                                             s_handlers.context);
            }
            continue;
        }
        if (event.type == MEDIA_EVENT_CALL_ACCEPT) {
            if (!connection_matches(event.connection)) continue;
            xiaotai_audio_set_uplink_enabled(true); /* call-active */
            int rc = xiaotai_audio_start(XIAOTAI_AUDIO_CODEC_ALAW_8K,
                                         send_encoded_audio, NULL);
            if (rc == BK_OK) {
                rc = normalize_tirtc_result(
                    TiRtcSubscribeAudio(event.connection, STREAM_UP_AUDIO_ID));
            }
            if (rc < 0) {
                BK_LOGE(TAG, "outbound call media start failed rc=%d\n", rc);
                (void)xiaotai_audio_stop();
                (void)queue_active_disconnect_once(
                    event.connection, event.generation, rc);
            } else if (s_handlers.on_call_connected != NULL) {
                BK_LOGI(TAG,
                        "device-call uplink active role=caller stream=%u subscription-gate=0\n",
                        STREAM_UP_AUDIO_ID);
                s_handlers.on_call_connected(event.generation,
                                             s_handlers.context);
            }
            continue;
        }
        if (event.type == MEDIA_EVENT_VOIP_CONNECTED) {
            if (connection_matches(event.connection) &&
                s_handlers.on_voip_connected != NULL) {
                s_handlers.on_voip_connected(event.generation,
                                             s_handlers.context);
            }
            continue;
        }
        if (event.type == MEDIA_EVENT_VOIP_ACCEPT) {
            if (!connection_matches(event.connection)) continue;
            xiaotai_audio_set_uplink_enabled(true); /* call-active */
            int rc = xiaotai_audio_start(XIAOTAI_AUDIO_CODEC_ALAW_8K,
                                         send_encoded_audio, NULL);
            if (rc == BK_OK) {
                rc = normalize_tirtc_result(
                    TiRtcSubscribeAudio(event.connection, VOIP_AUDIO_ID));
            }
            if (rc < 0) {
                BK_LOGE(TAG, "VoIP media start failed rc=%d\n", rc);
                (void)xiaotai_audio_stop();
                (void)queue_active_disconnect_once(
                    event.connection, event.generation, rc);
            } else if (s_handlers.on_voip_active != NULL) {
                BK_LOGI(TAG,
                        "VoIP uplink enabled by call-active gate stream=%u\n",
                        VOIP_AUDIO_ID);
                s_handlers.on_voip_active(event.generation,
                                          s_handlers.context);
            }
            continue;
        }
        if (event.type == MEDIA_EVENT_ROOM_CONNECTED) {
            if (!connection_matches(event.connection)) continue;
            if (s_handlers.on_room_connected != NULL) {
                s_handlers.on_room_connected(event.generation,
                                             s_handlers.context);
            }
            continue;
        }
        if (event.type == MEDIA_EVENT_AI_COMMAND) {
            if (s_handlers.on_ai_command != NULL) {
                s_handlers.on_ai_command(event.generation, event.command,
                                         event.data, event.length,
                                         s_handlers.context);
            }
            os_free(event.data);
            continue;
        }
        if (event.type == MEDIA_EVENT_ROOM_COMMAND) {
            if (s_handlers.on_room_command != NULL) {
                s_handlers.on_room_command(event.generation, event.command,
                                           event.data, event.length,
                                           s_handlers.context);
            }
            os_free(event.data);
            continue;
        }
        if (event.type == MEDIA_EVENT_VIDEO_START) {
            if (connection_matches(event.connection) &&
                !xiaotai_video_running()) {
                int rc = xiaotai_video_start(send_h264, NULL);
                if (rc != BK_OK) {
                    atomic_store_explicit(&s_video_subscribed, false,
                                          memory_order_release);
                    /* Keep remote audio alive when the optional camera is
                     * absent or cannot start. A video fault must not tear
                     * down an otherwise usable intercom session. */
                    BK_LOGE(TAG,
                            "STREAM video unavailable rc=%d; audio remains active\n",
                            rc);
                }
            }
            continue;
        }
        if (event.type == MEDIA_EVENT_VIDEO_STOP) {
            (void)xiaotai_video_stop();
            continue;
        }
        if (event.type == MEDIA_EVENT_KEYFRAME) {
            if (connection_matches(event.connection)) {
                (void)xiaotai_video_request_keyframe();
            }
            continue;
        }
        if (event.type == MEDIA_EVENT_CLOSED) {
            /* Temporary close-path probes: remove after the SMP stall has
             * been reproduced with an identified failing boundary. */
            BK_LOGI(TAG, "[DEBUG-bk-close] worker-received mode=%d generation=%u\n",
                    (int)event.mode, (unsigned)event.generation);
            (void)xiaotai_video_stop();
            BK_LOGI(TAG, "[DEBUG-bk-close] worker-video-stopped\n");
            if (event.mode == CONNECTION_AI && xiaotai_audio_running()) {
                xiaotai_audio_set_uplink_enabled(false);
                drain_ai_playback_after_transport_close();
            }
            (void)xiaotai_audio_stop();
            BK_LOGI(TAG, "[DEBUG-bk-close] worker-audio-stopped\n");
            if (event.mode == CONNECTION_AI &&
                s_handlers.on_ai_disconnected != NULL) {
                s_handlers.on_ai_disconnected(event.generation, event.error,
                                              s_handlers.context);
            } else if (event.mode == CONNECTION_CALL &&
                       s_handlers.on_call_disconnected != NULL) {
                s_handlers.on_call_disconnected(event.generation, event.error,
                                                s_handlers.context);
            } else if (event.mode == CONNECTION_VOIP &&
                       s_handlers.on_voip_disconnected != NULL) {
                s_handlers.on_voip_disconnected(event.generation, event.error,
                                                s_handlers.context);
            } else if (event.mode == CONNECTION_STREAM &&
                       s_handlers.on_stream_disconnected != NULL) {
                s_handlers.on_stream_disconnected(event.generation,
                                                  event.error,
                                                  s_handlers.context);
            } else if (event.mode == CONNECTION_ROOM &&
                       s_handlers.on_room_disconnected != NULL) {
                s_handlers.on_room_disconnected(event.generation, event.error,
                                                s_handlers.context);
            }
            BK_LOGI(TAG, "[DEBUG-bk-close] worker-handler-returned\n");
            if (event.mode == CONNECTION_ROOM) {
                xiaotai_audio_set_uplink_enabled(true);
            } else if (event.mode == CONNECTION_STREAM) {
                xiaotai_audio_set_uplink_enabled(true);
            }
            BK_LOGI(TAG, "connection closed mode=%d generation=%u error=%d\n",
                    event.mode, (unsigned)event.generation, event.error);
        }
    }
}

static int start_media_worker(void)
{
    if (s_media_queue != NULL) return BK_OK;
    int rc = rtos_init_queue(&s_media_queue, "xiaotai_tirtc_media",
                             sizeof(media_event_t), MEDIA_QUEUE_DEPTH);
    if (rc != BK_OK) return rc;
    rc = rtos_create_psram_thread(&s_media_thread, 5, "xiaotai_rtc_media",
                                  media_worker, 4096, NULL);
    if (rc != BK_OK) {
        rtos_deinit_queue(&s_media_queue);
        s_media_queue = NULL;
    }
    return rc;
}

static void on_event(int event, const void *data, int len)
{
    (void)data;
    (void)len;
    if (event == TIRTC_EVENT_SYS_STARTED) {
        s_state = XIAOTAI_TIRTC_READY;
        BK_LOGI(TAG, "SDK ready version=%s\n", TiRtcGetVersion());
    } else if (event == TIRTC_EVENT_SYS_STOPPED) {
        s_state = XIAOTAI_TIRTC_STOPPED;
        BK_LOGI(TAG, "SDK stopped\n");
    }
}

static void on_conn_accepted(tirtc_conn_t connection)
{
    if (atomic_load_explicit(&s_outgoing_connect_inflight,
                             memory_order_acquire)) {
        BK_LOGW(TAG, "inbound connection rejected during outgoing cleanup\n");
        (void)queue_media_event(MEDIA_EVENT_DISCONNECT, connection, 0,
                                TIRTC_E_BUSY);
        return;
    }
    uintptr_t expected = 0;
    if (!atomic_compare_exchange_strong_explicit(&s_connection,
                                                  &expected,
                                                  (uintptr_t)connection,
                                                  memory_order_acq_rel,
                                                  memory_order_acquire)) {
        BK_LOGW(TAG, "additional inbound connection rejected\n");
        (void)queue_media_event(MEDIA_EVENT_DISCONNECT, connection, 0,
                                TIRTC_E_BUSY);
        return;
    }
    atomic_store_explicit(&s_active_disconnect_queued, false,
                          memory_order_release);
    uint32_t outbound_generation = (uint32_t)atomic_exchange_explicit(
        &s_pending_outbound_call_generation, 0, memory_order_acq_rel);
    if (outbound_generation != 0U) {
        atomic_store_explicit(&s_active_generation, outbound_generation,
                              memory_order_release);
        atomic_store_explicit(&s_connection_mode, CONNECTION_CALL,
                              memory_order_release);
        atomic_store_explicit(&s_connection_error, 0, memory_order_release);
        atomic_store_explicit(&s_audio_subscribed, false, memory_order_release);
        atomic_store_explicit(&s_video_subscribed, false, memory_order_release);
        atomic_store_explicit(&s_call_is_caller, true, memory_order_release);
        atomic_store_explicit(&s_call_accept_seen, false, memory_order_release);
        BK_LOGI(TAG,
                "outbound device call transport accepted generation=%u room=%s\n",
                (unsigned)outbound_generation, s_active_call_room);
        return;
    }

    uint32_t generation = (uint32_t)atomic_fetch_add_explicit(
                              &s_generation_counter, 1,
                              memory_order_acq_rel) + 1U;
    if (generation == 0U) {
        generation = 1U;
        atomic_store_explicit(&s_generation_counter, generation,
                              memory_order_release);
    }
    atomic_store_explicit(&s_active_generation, generation,
                          memory_order_release);
    atomic_store_explicit(&s_connection_mode, CONNECTION_STREAM,
                          memory_order_release);
    atomic_store_explicit(&s_connection_error, 0, memory_order_release);
    atomic_store_explicit(&s_audio_subscribed, false, memory_order_release);
    atomic_store_explicit(&s_video_subscribed, false, memory_order_release);
    atomic_store_explicit(&s_voip_accept_seen, false, memory_order_release);
    atomic_store_explicit(&s_call_is_caller, false, memory_order_release);
    atomic_store_explicit(&s_call_accept_seen, false, memory_order_release);
    s_active_call_room[0] = '\0';
    BK_LOGI(TAG, "inbound STREAM accepted generation=%u\n",
            (unsigned)generation);
    if (!queue_media_event(MEDIA_EVENT_CONNECTED, connection, generation, 0)) {
        atomic_store_explicit(&s_connection_error, TIRTC_E_LACK_OF_RESOURCE,
                              memory_order_release);
        (void)queue_active_disconnect_once(connection, generation,
                                           TIRTC_E_LACK_OF_RESOURCE);
    }
}

static void on_conn_error(tirtc_conn_t connection, int error)
{
    if (!connection_matches(connection)) return;
    /* A submitted disconnect already owns teardown and its original result.
     * TiRtcDisconnect may subsequently emit a generic close error. Like the
     * ESP adapter's invalidated local handle, this callback must not re-enter
     * teardown or replace a successful local exit with that SDK error. Keep
     * the BK handle until on_disconnected so deferred room HTTP still waits
     * for transport release. */
    if (atomic_load_explicit(&s_active_disconnect_queued,
                             memory_order_acquire)) return;
    atomic_store_explicit(&s_connection_error, error, memory_order_release);
    BK_LOGW(TAG, "connection error=%d\n", error);
    (void)queue_active_disconnect_once(connection,
                                       xiaotai_tirtc_generation(), error);
}

static void on_disconnected(tirtc_conn_t connection)
{
    BK_LOGI(TAG, "TiRTC disconnected callback handle=%p\n", connection);
    uintptr_t cleanup = (uintptr_t)connection;
    if (atomic_compare_exchange_strong_explicit(
            &s_outgoing_cleanup_connection, &cleanup, 0,
            memory_order_acq_rel, memory_order_acquire)) {
        release_outgoing_connect();
        BK_LOGI(TAG,
                "outgoing cleanup complete handle=%p heap=%u psram=%u\n",
                connection, (unsigned)rtos_get_free_heap_size(),
                (unsigned)rtos_get_psram_free_heap_size());
        require_recovery_if_heap_low("failed-handle-cleanup");
    }
    BK_LOGI(TAG, "[DEBUG-bk-close] callback-cleanup-checked\n");
    uintptr_t expected = (uintptr_t)connection;
    if (!atomic_compare_exchange_strong_explicit(&s_connection,
                                                  &expected, 0,
                                                  memory_order_acq_rel,
                                                  memory_order_acquire)) {
        BK_LOGI(TAG, "[DEBUG-bk-close] callback-stale handle=%p\n", connection);
        return;
    }
    BK_LOGI(TAG, "[DEBUG-bk-close] callback-handle-cleared\n");
    atomic_store_explicit(&s_active_disconnect_queued, false,
                          memory_order_release);
    uint32_t generation = (uint32_t)atomic_exchange_explicit(
        &s_active_generation, 0, memory_order_acq_rel);
    int error = atomic_exchange_explicit(&s_connection_error, 0,
                                         memory_order_acq_rel);
    connection_mode_t mode = (connection_mode_t)atomic_exchange_explicit(
        &s_connection_mode, CONNECTION_NONE, memory_order_acq_rel);
    atomic_store_explicit(&s_audio_subscribed, false, memory_order_release);
    atomic_store_explicit(&s_video_subscribed, false, memory_order_release);
    if (mode == CONNECTION_CALL) {
        s_active_call_room[0] = '\0';
        atomic_store_explicit(&s_call_is_caller, false, memory_order_release);
        atomic_store_explicit(&s_call_accept_seen, false, memory_order_release);
    }
    if (mode == CONNECTION_VOIP) {
        atomic_store_explicit(&s_voip_accept_seen, false,
                              memory_order_release);
    }
    media_event_t event = {
        .type = MEDIA_EVENT_CLOSED,
        .generation = generation,
        .error = error,
        .mode = mode,
    };
    BK_LOGI(TAG, "[DEBUG-bk-close] callback-queue-begin mode=%d generation=%u\n",
            (int)mode, (unsigned)generation);
    if (rtos_push_to_queue(&s_media_queue, &event, 0) != BK_OK) {
        BK_LOGE(TAG, "media close event queue full\n");
    }
    BK_LOGI(TAG, "[DEBUG-bk-close] callback-queue-returned\n");
}

static void on_command(tirtc_conn_t connection, uint32_t command,
                       const void *data, uint32_t length)
{
    if (!connection_matches(connection)) {
        return;
    }
    connection_mode_t mode = (connection_mode_t)atomic_load_explicit(
        &s_connection_mode, memory_order_acquire);
    if (mode == CONNECTION_CALL) {
        if (command == 0x2000U && data != NULL && length > 0U &&
            atomic_load_explicit(&s_call_is_caller, memory_order_acquire)) {
            cJSON *root = cJSON_ParseWithLength((const char *)data, length);
            const cJSON *room = root == NULL ? NULL :
                cJSON_GetObjectItemCaseSensitive(root, "room_id");
            bool valid = cJSON_IsString(room) && room->valuestring != NULL &&
                         strcmp(room->valuestring, s_active_call_room) == 0;
            cJSON_Delete(root);
            bool already_seen = valid && atomic_exchange_explicit(
                &s_call_accept_seen, true, memory_order_acq_rel);
            if (!valid) {
                BK_LOGW(TAG, "outbound call ignored mismatched accept\n");
            } else if (!already_seen &&
                       !queue_media_event(MEDIA_EVENT_CALL_ACCEPT, connection,
                                          xiaotai_tirtc_generation(), 0)) {
                atomic_store_explicit(&s_connection_error,
                                      TIRTC_E_LACK_OF_RESOURCE,
                                      memory_order_release);
                (void)queue_active_disconnect_once(
                    connection, xiaotai_tirtc_generation(),
                    TIRTC_E_LACK_OF_RESOURCE);
            }
        } else if (command == 0x2001U) {
            BK_LOGI(TAG, "device call remote hangup command received\n");
            (void)queue_active_disconnect_once(
                connection, xiaotai_tirtc_generation(), 0);
        }
        return;
    }
    if (mode == CONNECTION_VOIP) {
        if (command == 0x2000U) {
            bool already_seen = atomic_exchange_explicit(
                &s_voip_accept_seen, true, memory_order_acq_rel);
            if (!already_seen &&
                !queue_media_event(MEDIA_EVENT_VOIP_ACCEPT, connection,
                                   xiaotai_tirtc_generation(), 0)) {
                atomic_store_explicit(&s_connection_error,
                                      TIRTC_E_LACK_OF_RESOURCE,
                                      memory_order_release);
                (void)queue_active_disconnect_once(
                    connection, xiaotai_tirtc_generation(),
                    TIRTC_E_LACK_OF_RESOURCE);
            }
        } else if (command == 0x2001U) {
            BK_LOGI(TAG, "VoIP remote hangup command received\n");
            (void)queue_active_disconnect_once(
                connection, xiaotai_tirtc_generation(), 0);
        }
        return;
    }
    if ((mode != CONNECTION_AI && mode != CONNECTION_ROOM) ||
        data == NULL || length == 0U) return;
    void *copy = os_malloc(length + 1U);
    if (copy == NULL) return;
    memcpy(copy, data, length);
    ((char *)copy)[length] = '\0';
    media_event_t event = {
        .type = mode == CONNECTION_ROOM ? MEDIA_EVENT_ROOM_COMMAND :
                                          MEDIA_EVENT_AI_COMMAND,
        .generation = xiaotai_tirtc_generation(),
        .mode = mode,
        .command = command,
        .data = copy,
        .length = length,
    };
    if (rtos_push_to_queue(&s_media_queue, &event, 0) != BK_OK) {
        os_free(copy);
        BK_LOGE(TAG, "AI command queue full\n");
    }
}

static void on_audio(tirtc_conn_t connection,
                     const TIRTCFRAMEINFO *frame,
                     void *data)
{
    if (frame == NULL || data == NULL || !connection_matches(connection)) return;
    connection_mode_t mode = (connection_mode_t)atomic_load_explicit(
        &s_connection_mode, memory_order_acquire);
    bool is_ai_opus = mode == CONNECTION_AI &&
                      frame->media == TIRTC_AUDIO_OPUS &&
                      frame->flags == TIRTC_AUDIOSAMPLE_16K16B1C;
    bool is_alaw = mode != CONNECTION_AI &&
                   frame->media == TIRTC_AUDIO_ALAW &&
                   frame->flags == TIRTC_AUDIOSAMPLE_8K16B1C;
    /* The stream id identifies the remote publisher; it is independent of
     * this device's local sending and subscription stream numbers. */
    if (!is_ai_opus && !is_alaw) {
        uint32_t rejected = (uint32_t)atomic_fetch_add_explicit(
                                &s_rejected_audio_frames, 1,
                                memory_order_relaxed) + 1U;
        if (rejected <= 3U || (rejected % 100U) == 0U) {
            BK_LOGW(TAG,
                    "unsupported downlink audio stream=%u media=%u flags=%u count=%u\n",
                    frame->stream_id, frame->media, frame->flags,
                    (unsigned)rejected);
        }
        return;
    }
    echo_diag_audio_rx(mode, frame);
    int rc = is_ai_opus ?
        xiaotai_audio_play_opus((const uint8_t *)data, frame->length) :
        xiaotai_audio_play_alaw((const uint8_t *)data, frame->length);
    uint32_t received = (uint32_t)atomic_fetch_add_explicit(
                            &s_received_audio_frames, 1,
                            memory_order_relaxed) + 1U;
    if (received == 1U || rc < 0) {
        if (received <= 3U || rc >= 0 || (received % 100U) == 0U) {
            BK_LOGI(TAG,
                    "downlink audio stream=%u media=%u flags=%u bytes=%u rc=%d count=%u\n",
                    frame->stream_id, frame->media, frame->flags,
                    (unsigned)frame->length, rc, (unsigned)received);
        }
    }
}

static int on_subscribe_audio(tirtc_conn_t connection, uint8_t stream_id)
{
    connection_mode_t mode = (connection_mode_t)atomic_load_explicit(
        &s_connection_mode, memory_order_acquire);
    uint8_t expected = mode == CONNECTION_AI ? AI_AUDIO_ID :
                       (mode == CONNECTION_ROOM ? ROOM_AUDIO_ID :
                        (mode == CONNECTION_VOIP ? VOIP_AUDIO_ID :
                                                  STREAM_UP_AUDIO_ID));
    bool active = connection_matches(connection) && stream_id == expected;
    bool supported = stream_id == VOIP_AUDIO_ID ||
                     stream_id == AI_AUDIO_ID ||
                     stream_id == STREAM_UP_AUDIO_ID;
    if (active) {
        atomic_store_explicit(&s_audio_subscribed, true,
                              memory_order_release);
        if (mode == CONNECTION_STREAM) {
            xiaotai_audio_set_uplink_enabled(true);
        }
    }
    /* TiRTC can deliver this callback while an outgoing WHIP connection is
     * still completing.  Acknowledge supported streams immediately and apply
     * active media ownership only after the connection handle is published. */
    BK_LOGI(TAG, "audio subscription stream=%u supported=%d active=%d\n",
            stream_id, supported, active);
    return supported ? 0 : -1;
}

static void on_unsubscribe_audio(tirtc_conn_t connection, uint8_t stream_id)
{
    connection_mode_t mode = (connection_mode_t)atomic_load_explicit(
        &s_connection_mode, memory_order_acquire);
    if (connection_matches(connection) &&
        stream_id == (mode == CONNECTION_AI ? AI_AUDIO_ID :
                     (mode == CONNECTION_ROOM ? ROOM_AUDIO_ID :
                      (mode == CONNECTION_VOIP ? VOIP_AUDIO_ID :
                                                STREAM_UP_AUDIO_ID)))) {
        atomic_store_explicit(&s_audio_subscribed, false,
                              memory_order_release);
        if (mode == CONNECTION_STREAM) {
            xiaotai_audio_set_uplink_enabled(false);
        }
    }
}

static int on_subscribe_video(tirtc_conn_t connection, uint8_t stream_id)
{
    bool supported = stream_id == STREAM_UP_VIDEO_ID;
    bool active = supported && connection_matches(connection);
    if (active) {
        atomic_store_explicit(&s_video_subscribed, true,
                              memory_order_release);
        active = queue_media_event(MEDIA_EVENT_VIDEO_START, connection,
                                   xiaotai_tirtc_generation(), 0);
        if (!active) {
            atomic_store_explicit(&s_video_subscribed, false,
                                  memory_order_release);
        }
    }
    BK_LOGI(TAG, "video subscription stream=%u supported=%d active=%d\n",
            stream_id, supported, active);
    return supported ? 0 : -1;
}

static void on_unsubscribe_video(tirtc_conn_t connection, uint8_t stream_id)
{
    if (connection_matches(connection) && stream_id == STREAM_UP_VIDEO_ID) {
        atomic_store_explicit(&s_video_subscribed, false,
                              memory_order_release);
        (void)queue_media_event(MEDIA_EVENT_VIDEO_STOP, connection,
                                xiaotai_tirtc_generation(), 0);
    }
}

static void on_request_key_frame(tirtc_conn_t connection, uint8_t stream_id)
{
    if (connection_matches(connection) && stream_id == STREAM_UP_VIDEO_ID) {
        BK_LOGI(TAG, "remote requested H264 keyframe stream=%u\n", stream_id);
        (void)queue_media_event(MEDIA_EVENT_KEYFRAME, connection,
                                xiaotai_tirtc_generation(), 0);
    }
}

static const TIRTCCALLBACKS s_callbacks = {
    .on_event = on_event,
    .on_conn_accepted = on_conn_accepted,
    .on_conn_error = on_conn_error,
    .on_disconnected = on_disconnected,
    .on_audio = on_audio,
    .on_command = on_command,
    .on_request_key_frame = on_request_key_frame,
    .on_subscribe_video = on_subscribe_video,
    .on_unsubscribe_video = on_unsubscribe_video,
    .on_subscribe_audio = on_subscribe_audio,
    .on_unsubscribe_audio = on_unsubscribe_audio,
};

void xiaotai_tirtc_set_handlers(const xiaotai_tirtc_handlers_t *handlers)
{
    if (handlers == NULL) {
        memset(&s_handlers, 0, sizeof(s_handlers));
    } else {
        s_handlers = *handlers;
    }
}

static int set_string_option(TIRTCOPTION option, const char *value)
{
    if (value == NULL || value[0] == '\0') return 0;
    /* The SDK's explicit length excludes the trailing NUL.  This matches the
     * TiRTC C reference for SERVICE_ENDPOINT, DEVICE_SECRET_KEY and CLIENT_ID.
     * Including NUL changes the byte identity of CLIENT_ID in SDK builds that
     * retain the option as a length-delimited string. */
    return TiRtcSetOption(option, value, (uint32_t)strlen(value));
}

static int set_poll_timeout_option(int requested_ms)
{
    if (requested_ms <= 0) return 0;

    int timeout_ms = requested_ms;
    int applied_ms = TiRtcSetOption(TIRTC_OPT_TGTRP_POLL_TIMEOUT,
                                    &timeout_ms,
                                    sizeof(timeout_ms));
    /* Unlike the other options, TGTRP_POLL_TIMEOUT returns the actual
     * timeout (1..50 ms) on success.  Treating that positive value as an
     * error caused an Init/Uninit retry loop and eventually corrupted the
     * SDK's RTOS task state. */
    if (applied_ms > 0) {
        BK_LOGI(TAG, "TiRTC transport poll timeout=%d ms\n", applied_ms);
        return 0;
    }
    return applied_ms;
}

int xiaotai_tirtc_start(const xiaotai_tirtc_config_t *config)
{
    if (config == NULL || config->device_id == NULL ||
        config->device_id[0] == '\0' || config->device_secret == NULL ||
        config->device_secret[0] == '\0') {
        return TIRTC_E_INVALID_PARAMETER;
    }
    if (s_state != XIAOTAI_TIRTC_STOPPED || s_initialized) {
        return TIRTC_E_BUSY;
    }

    s_state = XIAOTAI_TIRTC_STARTING;
    TiRtcLogSetCallback(tirtc_sdk_log_callback);
    if (s_log_level == XIAOTAI_TIRTC_LOG_LEVEL_SDK_DEFAULT) {
        BK_LOGI(TAG, "TiRTC SDK serial callback installed log level=SDK default\n");
    } else {
        BK_LOGI(TAG,
                "TiRTC SDK serial callback installed log level=%d (tgtrp=%d)\n",
                s_log_level, s_log_level > 10 ? s_log_level - 10 : 0);
    }
    BK_LOGI(TAG,
            "TiRTC start args device_id=%s device-id-len=%u client_id=%s client-id-len=%u endpoint=%s endpoint-len=%u secret-len=%u app-id-len=%u\n",
            config->device_id, (unsigned)strlen(config->device_id),
            config->client_id == NULL ? "<none>" : config->client_id,
            config->client_id == NULL ? 0U :
                (unsigned)strlen(config->client_id),
            config->service_endpoint == NULL ? "<default>" :
                config->service_endpoint,
            config->service_endpoint == NULL ? 0U :
                (unsigned)strlen(config->service_endpoint),
            (unsigned)strlen(config->device_secret),
            config->app_id == NULL ? 0U : (unsigned)strlen(config->app_id));
    int rc;
    if (config->max_send_buffer > 0) {
        uint32_t bytes = (uint32_t)config->max_send_buffer;
        rc = TiRtcSetOption(TIRTC_OPT_MAX_SEND_BUFFER, &bytes, sizeof(bytes));
        if (rc != 0) goto fail;
    }
    rc = TiRtcInit();
    if (rc != 0) goto fail;
    s_initialized = true;
    BK_LOGI(TAG, "TiRTC initialization completed\n");

    rc = start_media_worker();
    if (rc != BK_OK) goto rollback;

    rc = set_string_option(TIRTC_OPT_DEVICE_SECRET_KEY,
                           config->device_secret);
    if (rc == 0) rc = set_string_option(TIRTC_OPT_CLIENT_ID, config->client_id);
    if (rc == 0) rc = set_string_option(TIRTC_OPT_APP_ID, config->app_id);
    if (rc == 0) {
        rc = set_string_option(TIRTC_OPT_SERVICE_ENDPOINT,
                               config->service_endpoint);
    }
    if (rc == 0) {
        int network_type = TIRTC_NETCONN_WIFI;
        rc = TiRtcSetOption(TIRTC_OPT_NETWORK_TYPE,
                            &network_type,
                            sizeof(network_type));
    }
    if (rc == 0 && config->max_connections > 0) {
        int count = config->max_connections;
        rc = TiRtcSetOption(TIRTC_OPT_MAX_CONNECTIONS,
                            &count,
                            sizeof(count));
    }
    if (rc == 0) rc = set_poll_timeout_option(config->poll_timeout_ms);
    if (rc == 0) {
        BK_LOGI(TAG, "TiRTC options configured; submitting SDK start\n");
        rc = TiRtcStart(config->device_id, &s_callbacks);
    }
    if (rc == 0) {
        BK_LOGI(TAG, "TiRTC SDK start submitted\n");
        return 0;
    }

rollback:
    TiRtcUninit();
    s_initialized = false;
fail:
    /* Submission failures are retryable after rollback; no SDK instance owns
     * resources at this point. Keep FAILED for failures during a live stop. */
    s_state = XIAOTAI_TIRTC_STOPPED;
    BK_LOGE(TAG, "SDK start submission failed rc=%d (%s)\n",
            rc, TiRtcGetErrorStr(rc));
    return rc;
}

static void on_ai_connect(int error, tirtc_conn_t connection,
                          void *user_data)
{
    uint32_t requested = (uint32_t)(uintptr_t)user_data;
    BK_LOGD(TAG,
            "TiRtcWhipConnect callback mode=ai error=%d connection=%p generation=%u\n",
            error, connection, (unsigned)requested);
    uint32_t pending = (uint32_t)atomic_exchange_explicit(
        &s_pending_ai_generation, 0, memory_order_acq_rel);
    if (pending != requested || requested == 0U) {
        if (connection != NULL) {
            (void)queue_outgoing_cleanup(connection, requested, "stale-ai");
        } else {
            release_outgoing_without_handle("stale-ai-no-handle");
        }
        return;
    }
    if (error != 0 || connection == NULL) {
        int failure = error != 0 ? error : TIRTC_E_CONN_OTHER_ERROR;
        BK_LOGW(TAG,
                "AI WHIP connect failed rc=%d (%s) generation=%u handle=%p heap=%u psram=%u\n",
                failure, TiRtcGetErrorStr(failure), (unsigned)requested,
                connection, (unsigned)rtos_get_free_heap_size(),
                (unsigned)rtos_get_psram_free_heap_size());
        if (connection != NULL) {
            (void)queue_outgoing_cleanup(connection, requested, "failed-ai");
        } else {
            release_outgoing_without_handle("failed-ai-no-handle");
        }
        media_event_t event = {
            .type = MEDIA_EVENT_CLOSED,
            .generation = requested,
            .error = failure,
            .mode = CONNECTION_AI,
        };
        (void)rtos_push_to_queue(&s_media_queue, &event, 0);
        return;
    }

    uintptr_t expected = 0;
    if (!atomic_compare_exchange_strong_explicit(&s_connection, &expected,
                                                  (uintptr_t)connection,
                                                  memory_order_acq_rel,
                                                  memory_order_acquire)) {
        (void)queue_outgoing_cleanup(connection, requested, "conflict-ai");
        media_event_t event = {
            .type = MEDIA_EVENT_CLOSED,
            .generation = requested,
            .error = TIRTC_E_BUSY,
            .mode = CONNECTION_AI,
        };
        (void)rtos_push_to_queue(&s_media_queue, &event, 0);
        return;
    }
    release_outgoing_connect();
    atomic_store_explicit(&s_active_disconnect_queued, false,
                          memory_order_release);
    atomic_store_explicit(&s_active_generation, requested,
                          memory_order_release);
    atomic_store_explicit(&s_connection_mode, CONNECTION_AI,
                          memory_order_release);
    atomic_store_explicit(&s_connection_error, 0, memory_order_release);
    atomic_store_explicit(&s_audio_subscribed, false, memory_order_release);
    atomic_store_explicit(&s_video_subscribed, false, memory_order_release);
    atomic_store_explicit(&s_call_is_caller, false, memory_order_release);
    atomic_store_explicit(&s_call_accept_seen, false, memory_order_release);
    BK_LOGI(TAG, "AI WHIP connected generation=%u\n", (unsigned)requested);
    if (!queue_media_event(MEDIA_EVENT_AI_CONNECTED, connection,
                           requested, 0)) {
        atomic_store_explicit(&s_connection_error,
                              TIRTC_E_LACK_OF_RESOURCE,
                              memory_order_release);
        (void)queue_active_disconnect_once(connection, requested,
                                           TIRTC_E_LACK_OF_RESOURCE);
    }
}

int xiaotai_tirtc_service_request(const char *path,
                                  TIRTCSERVICEREQUESTCALLBACK callback,
                                  void *context)
{
    if (!xiaotai_tirtc_ready() || path == NULL || callback == NULL) {
        return TIRTC_E_INVALID_PARAMETER;
    }
    return TiRtcServiceRequest(path, NULL, NULL, callback, context);
}

int xiaotai_tirtc_service_request_json(const char *path, const char *json,
                                       TIRTCSERVICEREQUESTCALLBACK callback,
                                       void *context)
{
    if (!xiaotai_tirtc_ready() || path == NULL || json == NULL ||
        callback == NULL) {
        return TIRTC_E_INVALID_PARAMETER;
    }
    return TiRtcServiceRequest(path, json, NULL, callback, context);
}

typedef struct {
    uint32_t generation;
    char room_id[128];
} call_connect_context_t;

static void on_call_connect(int error, tirtc_conn_t connection,
                            void *user_data)
{
    call_connect_context_t *context = user_data;
    uint32_t requested = context == NULL ? 0U : context->generation;
    BK_LOGD(TAG,
            "TiRtcConnect callback error=%d connection=%p generation=%u\n",
            error, connection, (unsigned)requested);
    char room_id[128] = {0};
    if (context != NULL) {
        snprintf(room_id, sizeof(room_id), "%s", context->room_id);
        os_free(context);
    }
    uint32_t pending = (uint32_t)atomic_exchange_explicit(
        &s_pending_call_generation, 0, memory_order_acq_rel);
    if (pending != requested || requested == 0U) {
        if (connection != NULL) {
            (void)queue_outgoing_cleanup(connection, requested, "stale-call");
        } else {
            release_outgoing_without_handle("stale-call-no-handle");
        }
        return;
    }
    if (error != 0 || connection == NULL) {
        if (connection != NULL) {
            (void)queue_outgoing_cleanup(connection, requested, "failed-call");
        } else {
            release_outgoing_without_handle("failed-call-no-handle");
        }
        media_event_t event = {
            .type = MEDIA_EVENT_CLOSED,
            .generation = requested,
            .error = error != 0 ? error : TIRTC_E_CONN_OTHER_ERROR,
            .mode = CONNECTION_CALL,
        };
        (void)rtos_push_to_queue(&s_media_queue, &event, 0);
        return;
    }

    uintptr_t expected = 0;
    if (!atomic_compare_exchange_strong_explicit(&s_connection, &expected,
                                                  (uintptr_t)connection,
                                                  memory_order_acq_rel,
                                                  memory_order_acquire)) {
        (void)queue_outgoing_cleanup(connection, requested, "conflict-call");
        media_event_t event = {
            .type = MEDIA_EVENT_CLOSED,
            .generation = requested,
            .error = TIRTC_E_BUSY,
            .mode = CONNECTION_CALL,
        };
        (void)rtos_push_to_queue(&s_media_queue, &event, 0);
        return;
    }
    release_outgoing_connect();
    atomic_store_explicit(&s_active_disconnect_queued, false,
                          memory_order_release);
    atomic_store_explicit(&s_active_generation, requested,
                          memory_order_release);
    atomic_store_explicit(&s_connection_mode, CONNECTION_CALL,
                          memory_order_release);
    atomic_store_explicit(&s_connection_error, 0, memory_order_release);
    atomic_store_explicit(&s_audio_subscribed, false, memory_order_release);
    atomic_store_explicit(&s_video_subscribed, false, memory_order_release);
    atomic_store_explicit(&s_call_is_caller, false, memory_order_release);
    atomic_store_explicit(&s_call_accept_seen, false, memory_order_release);
    snprintf(s_active_call_room, sizeof(s_active_call_room), "%s", room_id);
    if (!queue_media_event(MEDIA_EVENT_CALL_CONNECTED, connection,
                           requested, 0)) {
        atomic_store_explicit(&s_connection_error,
                              TIRTC_E_LACK_OF_RESOURCE,
                              memory_order_release);
        (void)queue_active_disconnect_once(connection, requested,
                                           TIRTC_E_LACK_OF_RESOURCE);
        return;
    }
    BK_LOGI(TAG, "device call connected generation=%u room=%s\n",
            (unsigned)requested, room_id);
}

int xiaotai_tirtc_call_connect(const char *peer_id, const char *token,
                               const char *room_id, uint32_t generation)
{
    if (!xiaotai_tirtc_ready() || peer_id == NULL || peer_id[0] == '\0' ||
        token == NULL || token[0] == '\0' || room_id == NULL ||
        room_id[0] == '\0' || generation == 0U || xiaotai_tirtc_busy()) {
        return TIRTC_E_INVALID_PARAMETER;
    }
    if (!claim_outgoing_connect()) return TIRTC_E_BUSY;
    uint32_t expected = 0;
    if (!atomic_compare_exchange_strong_explicit(&s_pending_call_generation,
                                                  &expected, generation,
                                                  memory_order_acq_rel,
                                                  memory_order_acquire)) {
        release_outgoing_connect();
        return TIRTC_E_BUSY;
    }
    call_connect_context_t *context = os_malloc(sizeof(*context));
    if (context == NULL) {
        atomic_store_explicit(&s_pending_call_generation, 0,
                              memory_order_release);
        release_outgoing_connect();
        return TIRTC_E_LACK_OF_RESOURCE;
    }
    context->generation = generation;
    snprintf(context->room_id, sizeof(context->room_id), "%s", room_id);
    BK_LOGD(TAG,
            "TiRtcConnect request peer_id=%s room_id=%s generation=%u\n",
            peer_id, room_id, (unsigned)generation);
    int rc = TiRtcConnect(peer_id, token, on_call_connect, context);
    BK_LOGD(TAG, "TiRtcConnect result rc=%d generation=%u\n",
            rc, (unsigned)generation);
    if (rc != 0) {
        atomic_store_explicit(&s_pending_call_generation, 0,
                              memory_order_release);
        release_outgoing_connect();
        os_free(context);
    }
    return rc;
}

int xiaotai_tirtc_expect_outbound_call(const char *room_id,
                                       uint32_t generation)
{
    if (!xiaotai_tirtc_ready() || room_id == NULL || room_id[0] == '\0' ||
        strlen(room_id) >= sizeof(s_active_call_room) || generation == 0U ||
        xiaotai_tirtc_busy() ||
        atomic_load_explicit(&s_pending_outbound_call_generation,
                             memory_order_acquire) != 0U) {
        return TIRTC_E_INVALID_PARAMETER;
    }
    snprintf(s_active_call_room, sizeof(s_active_call_room), "%s", room_id);
    uint32_t expected = 0U;
    if (!atomic_compare_exchange_strong_explicit(
            &s_pending_outbound_call_generation, &expected, generation,
            memory_order_release, memory_order_acquire)) {
        s_active_call_room[0] = '\0';
        return TIRTC_E_BUSY;
    }
    BK_LOGI(TAG, "waiting outbound device call generation=%u room=%s\n",
            (unsigned)generation, room_id);
    return 0;
}

static void on_voip_connect(int error, tirtc_conn_t connection,
                            void *user_data)
{
    uint32_t requested = (uint32_t)(uintptr_t)user_data;
    BK_LOGD(TAG,
            "TiRtcWhipConnect callback mode=voip error=%d connection=%p generation=%u\n",
            error, connection, (unsigned)requested);
    uint32_t pending = (uint32_t)atomic_exchange_explicit(
        &s_pending_voip_generation, 0, memory_order_acq_rel);
    if (pending != requested || requested == 0U) {
        if (connection != NULL) {
            (void)queue_outgoing_cleanup(connection, requested, "stale-voip");
        } else {
            release_outgoing_without_handle("stale-voip-no-handle");
        }
        return;
    }
    if (error != 0 || connection == NULL) {
        if (connection != NULL) {
            (void)queue_outgoing_cleanup(connection, requested, "failed-voip");
        } else {
            release_outgoing_without_handle("failed-voip-no-handle");
        }
        media_event_t event = {
            .type = MEDIA_EVENT_CLOSED,
            .generation = requested,
            .error = error != 0 ? error : TIRTC_E_CONN_OTHER_ERROR,
            .mode = CONNECTION_VOIP,
        };
        (void)rtos_push_to_queue(&s_media_queue, &event, 0);
        return;
    }
    uintptr_t expected = 0;
    if (!atomic_compare_exchange_strong_explicit(&s_connection, &expected,
                                                  (uintptr_t)connection,
                                                  memory_order_acq_rel,
                                                  memory_order_acquire)) {
        (void)queue_outgoing_cleanup(connection, requested, "conflict-voip");
        media_event_t event = {
            .type = MEDIA_EVENT_CLOSED,
            .generation = requested,
            .error = TIRTC_E_BUSY,
            .mode = CONNECTION_VOIP,
        };
        (void)rtos_push_to_queue(&s_media_queue, &event, 0);
        return;
    }
    release_outgoing_connect();
    atomic_store_explicit(&s_active_disconnect_queued, false,
                          memory_order_release);
    atomic_store_explicit(&s_active_generation, requested,
                          memory_order_release);
    atomic_store_explicit(&s_connection_mode, CONNECTION_VOIP,
                          memory_order_release);
    atomic_store_explicit(&s_connection_error, 0, memory_order_release);
    atomic_store_explicit(&s_audio_subscribed, false, memory_order_release);
    atomic_store_explicit(&s_video_subscribed, false, memory_order_release);
    atomic_store_explicit(&s_voip_accept_seen, false, memory_order_release);
    BK_LOGI(TAG, "VoIP WHIP connected generation=%u; waiting command 0x2000\n",
            (unsigned)requested);
    if (!queue_media_event(MEDIA_EVENT_VOIP_CONNECTED, connection,
                           requested, 0)) {
        atomic_store_explicit(&s_connection_error,
                              TIRTC_E_LACK_OF_RESOURCE,
                              memory_order_release);
        (void)queue_active_disconnect_once(connection, requested,
                                           TIRTC_E_LACK_OF_RESOURCE);
    }
}

int xiaotai_tirtc_voip_connect(const char *peer_id, const char *token,
                               uint32_t generation)
{
    if (!xiaotai_tirtc_ready() || peer_id == NULL || peer_id[0] == '\0' ||
        token == NULL || token[0] == '\0' || generation == 0U ||
        xiaotai_tirtc_busy()) {
        return TIRTC_E_INVALID_PARAMETER;
    }
    if (!claim_outgoing_connect()) return TIRTC_E_BUSY;
    uint32_t expected = 0;
    if (!atomic_compare_exchange_strong_explicit(&s_pending_voip_generation,
                                                  &expected, generation,
                                                  memory_order_acq_rel,
                                                  memory_order_acquire)) {
        release_outgoing_connect();
        return TIRTC_E_BUSY;
    }
    BK_LOGD(TAG,
            "TiRtcWhipConnect request mode=voip generation=%u\n",
            (unsigned)generation);
    int rc = TiRtcWhipConnect(peer_id, token, on_voip_connect,
                              (void *)(uintptr_t)generation);
    BK_LOGD(TAG, "TiRtcWhipConnect result mode=voip rc=%d generation=%u\n",
            rc, (unsigned)generation);
    if (rc != 0) {
        atomic_store_explicit(&s_pending_voip_generation, 0,
                              memory_order_release);
        release_outgoing_connect();
    }
    return rc;
}

static void on_room_connect(int error, tirtc_conn_t connection,
                            void *user_data)
{
    uint32_t requested = (uint32_t)(uintptr_t)user_data;
    BK_LOGD(TAG,
            "TiRtcWhipConnect callback mode=room error=%d connection=%p generation=%u\n",
            error, connection, (unsigned)requested);
    uint32_t pending = (uint32_t)atomic_exchange_explicit(
        &s_pending_room_generation, 0, memory_order_acq_rel);
    if (pending != requested || requested == 0U) {
        if (connection != NULL) {
            (void)queue_outgoing_cleanup(connection, requested, "stale-room");
        } else {
            release_outgoing_without_handle("stale-room-no-handle");
        }
        return;
    }
    if (error != 0 || connection == NULL) {
        if (connection != NULL) {
            (void)queue_outgoing_cleanup(connection, requested, "failed-room");
        } else {
            release_outgoing_without_handle("failed-room-no-handle");
        }
        media_event_t event = {
            .type = MEDIA_EVENT_CLOSED,
            .generation = requested,
            .error = error != 0 ? error : TIRTC_E_CONN_OTHER_ERROR,
            .mode = CONNECTION_ROOM,
        };
        (void)rtos_push_to_queue(&s_media_queue, &event, 0);
        return;
    }
    uintptr_t expected = 0;
    if (!atomic_compare_exchange_strong_explicit(&s_connection, &expected,
                                                  (uintptr_t)connection,
                                                  memory_order_acq_rel,
                                                  memory_order_acquire)) {
        (void)queue_outgoing_cleanup(connection, requested, "conflict-room");
        media_event_t event = {
            .type = MEDIA_EVENT_CLOSED,
            .generation = requested,
            .error = TIRTC_E_BUSY,
            .mode = CONNECTION_ROOM,
        };
        (void)rtos_push_to_queue(&s_media_queue, &event, 0);
        return;
    }
    release_outgoing_connect();
    atomic_store_explicit(&s_active_disconnect_queued, false,
                          memory_order_release);
    atomic_store_explicit(&s_active_generation, requested,
                          memory_order_release);
    atomic_store_explicit(&s_connection_mode, CONNECTION_ROOM,
                          memory_order_release);
    atomic_store_explicit(&s_connection_error, 0, memory_order_release);
    atomic_store_explicit(&s_audio_subscribed, false, memory_order_release);
    atomic_store_explicit(&s_video_subscribed, false, memory_order_release);
    if (!queue_media_event(MEDIA_EVENT_ROOM_CONNECTED, connection,
                           requested, 0)) {
        atomic_store_explicit(&s_connection_error,
                              TIRTC_E_LACK_OF_RESOURCE,
                              memory_order_release);
        (void)queue_active_disconnect_once(connection, requested,
                                           TIRTC_E_LACK_OF_RESOURCE);
    }
}

int xiaotai_tirtc_room_connect(const char *peer_id, const char *token,
                               uint32_t generation)
{
    if (!xiaotai_tirtc_ready() || peer_id == NULL || peer_id[0] == '\0' ||
        token == NULL || token[0] == '\0' || generation == 0U ||
        xiaotai_tirtc_busy()) {
        return TIRTC_E_INVALID_PARAMETER;
    }
    if (!claim_outgoing_connect()) return TIRTC_E_BUSY;
    uint32_t expected = 0;
    if (!atomic_compare_exchange_strong_explicit(&s_pending_room_generation,
                                                  &expected, generation,
                                                  memory_order_acq_rel,
                                                  memory_order_acquire)) {
        release_outgoing_connect();
        return TIRTC_E_BUSY;
    }
    BK_LOGD(TAG,
            "TiRtcWhipConnect request mode=room generation=%u\n",
            (unsigned)generation);
    int rc = TiRtcWhipConnect(peer_id, token, on_room_connect,
                              (void *)(uintptr_t)generation);
    BK_LOGD(TAG, "TiRtcWhipConnect result mode=room rc=%d generation=%u\n",
            rc, (unsigned)generation);
    if (rc != 0) {
        atomic_store_explicit(&s_pending_room_generation, 0,
                              memory_order_release);
        release_outgoing_connect();
    }
    return rc;
}

int xiaotai_tirtc_ai_connect(const char *peer_id, const char *token,
                             uint32_t generation)
{
    if (!xiaotai_tirtc_ready() || peer_id == NULL || peer_id[0] == '\0' ||
        token == NULL || token[0] == '\0' || generation == 0U ||
        xiaotai_tirtc_busy()) {
        return TIRTC_E_INVALID_PARAMETER;
    }
    if (!claim_outgoing_connect()) return TIRTC_E_BUSY;
    uint32_t expected = 0;
    if (!atomic_compare_exchange_strong_explicit(&s_pending_ai_generation,
                                                  &expected, generation,
                                                  memory_order_acq_rel,
                                                  memory_order_acquire)) {
        release_outgoing_connect();
        return TIRTC_E_BUSY;
    }
    BK_LOGI(TAG,
            "AI WHIP memory before submit heap=%u min=%u psram=%u psram-min=%u\n",
            (unsigned)rtos_get_free_heap_size(),
            (unsigned)rtos_get_minimum_free_heap_size(),
            (unsigned)rtos_get_psram_free_heap_size(),
            (unsigned)rtos_get_psram_minimum_free_heap_size());
    BK_LOGD(TAG,
            "TiRtcWhipConnect args mode=ai callback=on_ai_connect generation=%u\n",
            (unsigned)generation);
    int rc = TiRtcWhipConnect(peer_id, token, on_ai_connect,
                              (void *)(uintptr_t)generation);
    BK_LOGD(TAG, "TiRtcWhipConnect returned rc=%d generation=%u\n",
            rc, (unsigned)generation);
    if (rc != 0) {
        atomic_store_explicit(&s_pending_ai_generation, 0,
                              memory_order_release);
        release_outgoing_connect();
    }
    return rc;
}

int xiaotai_tirtc_ai_prepare_media(void)
{
    tirtc_conn_t connection = (tirtc_conn_t)atomic_load_explicit(
        &s_connection, memory_order_acquire);
    if (connection == NULL ||
        atomic_load_explicit(&s_connection_mode, memory_order_acquire) !=
            CONNECTION_AI) {
        return TIRTC_E_INVALID_HANDLE;
    }
    xiaotai_audio_set_uplink_enabled(false);
    atomic_store_explicit(&s_audio_subscribed, false, memory_order_release);
    int rc = xiaotai_audio_start(XIAOTAI_AUDIO_CODEC_OPUS_16K,
                                 send_encoded_audio, NULL);
    if (rc == BK_OK) {
        rc = TiRtcSubscribeAudio(connection, AI_AUDIO_ID);
    }
    if (rc < 0) {
        (void)xiaotai_audio_stop();
    }
    return normalize_tirtc_result(rc);
}

int xiaotai_tirtc_ai_start_media(void)
{
    tirtc_conn_t connection = (tirtc_conn_t)atomic_load_explicit(
        &s_connection, memory_order_acquire);
    if (connection == NULL ||
        atomic_load_explicit(&s_connection_mode, memory_order_acquire) !=
            CONNECTION_AI || !xiaotai_audio_running()) {
        return TIRTC_E_INVALID_HANDLE;
    }
    int rc = normalize_tirtc_result(
        TiRtcSubscribeAudio(connection, AI_AUDIO_ID));
    if (rc == BK_OK) {
        atomic_store_explicit(&s_audio_subscribed, true,
                              memory_order_release);
        xiaotai_audio_set_uplink_enabled(true);
    }
    return rc;
}

int xiaotai_tirtc_room_start_media(void)
{
    tirtc_conn_t connection = (tirtc_conn_t)atomic_load_explicit(
        &s_connection, memory_order_acquire);
    if (connection == NULL ||
        atomic_load_explicit(&s_connection_mode, memory_order_acquire) !=
            CONNECTION_ROOM) {
        return TIRTC_E_INVALID_HANDLE;
    }
    xiaotai_audio_set_uplink_enabled(false);
    int rc = xiaotai_audio_start(XIAOTAI_AUDIO_CODEC_ALAW_8K,
                                 send_encoded_audio, NULL);
    if (rc == BK_OK) rc = TiRtcSubscribeAudio(connection, ROOM_AUDIO_ID);
    if (rc < 0) (void)xiaotai_audio_stop();
    return normalize_tirtc_result(rc);
}

int xiaotai_tirtc_send_command(uint32_t command,
                               const void *data, uint32_t length)
{
    tirtc_conn_t connection = (tirtc_conn_t)atomic_load_explicit(
        &s_connection, memory_order_acquire);
    return connection == NULL ? TIRTC_E_INVALID_HANDLE
                              : TiRtcSendCommand(connection, command,
                                                 data, length);
}

int xiaotai_tirtc_disconnect(void)
{
    atomic_store_explicit(&s_pending_ai_generation, 0, memory_order_release);
    atomic_store_explicit(&s_pending_call_generation, 0,
                          memory_order_release);
    atomic_store_explicit(&s_pending_outbound_call_generation, 0,
                          memory_order_release);
    atomic_store_explicit(&s_pending_voip_generation, 0,
                          memory_order_release);
    atomic_store_explicit(&s_pending_room_generation, 0,
                          memory_order_release);
    tirtc_conn_t connection = (tirtc_conn_t)atomic_load_explicit(
        &s_connection, memory_order_acquire);
    if (connection == NULL) return TIRTC_E_INVALID_HANDLE;
    return queue_active_disconnect_once(connection,
                                        xiaotai_tirtc_generation(), 0) ?
           BK_OK : TIRTC_E_LACK_OF_RESOURCE;
}

int xiaotai_tirtc_stop(void)
{
    if (s_state == XIAOTAI_TIRTC_STOPPED) return 0;
    s_state = XIAOTAI_TIRTC_STOPPING;
    int rc = TiRtcStop();
    if (rc != 0) {
        s_state = XIAOTAI_TIRTC_FAILED;
        return rc;
    }
    return 0;
}

int xiaotai_tirtc_finalize_stop(void)
{
    if (s_state != XIAOTAI_TIRTC_STOPPED) return TIRTC_E_BUSY;
    if (s_initialized) {
        TiRtcUninit();
        s_initialized = false;
    }
    atomic_store_explicit(&s_connection, 0, memory_order_release);
    atomic_store_explicit(&s_active_generation, 0, memory_order_release);
    atomic_store_explicit(&s_connection_error, 0, memory_order_release);
    atomic_store_explicit(&s_connection_mode, CONNECTION_NONE,
                          memory_order_release);
    atomic_store_explicit(&s_pending_ai_generation, 0, memory_order_release);
    atomic_store_explicit(&s_pending_call_generation, 0, memory_order_release);
    atomic_store_explicit(&s_pending_outbound_call_generation, 0,
                          memory_order_release);
    atomic_store_explicit(&s_pending_voip_generation, 0,
                          memory_order_release);
    atomic_store_explicit(&s_pending_room_generation, 0,
                          memory_order_release);
    atomic_store_explicit(&s_outgoing_connect_inflight, false,
                          memory_order_release);
    atomic_store_explicit(&s_outgoing_cleanup_connection, 0,
                          memory_order_release);
    atomic_store_explicit(&s_cleanup_probe_active, false,
                          memory_order_release);
    atomic_store_explicit(&s_recovery_required, false,
                          memory_order_release);
    atomic_store_explicit(&s_audio_subscribed, false, memory_order_release);
    atomic_store_explicit(&s_video_subscribed, false, memory_order_release);
    return 0;
}

xiaotai_tirtc_state_t xiaotai_tirtc_state(void)
{
    return s_state;
}

bool xiaotai_tirtc_ready(void)
{
    return s_state == XIAOTAI_TIRTC_READY;
}

bool xiaotai_tirtc_connected(void)
{
    return atomic_load_explicit(&s_connection, memory_order_acquire) != 0;
}

bool xiaotai_tirtc_busy(void)
{
    return xiaotai_tirtc_connected() ||
           atomic_load_explicit(&s_outgoing_connect_inflight,
                                memory_order_acquire);
}

bool xiaotai_tirtc_recovery_required(void)
{
    if (atomic_load_explicit(&s_cleanup_probe_active, memory_order_acquire)) {
        uint32_t started = (uint32_t)atomic_load_explicit(
            &s_cleanup_probe_started_ms, memory_order_acquire);
        uint32_t now = rtos_get_time();
        if ((int32_t)(now - (started + OUTGOING_CLEANUP_GRACE_MS)) >= 0) {
            uint32_t baseline = (uint32_t)atomic_load_explicit(
                &s_outgoing_heap_baseline, memory_order_acquire);
            uint32_t free_heap = rtos_get_free_heap_size();
            uint32_t retained = baseline > free_heap ? baseline - free_heap : 0U;
            atomic_store_explicit(&s_cleanup_probe_active, false,
                                  memory_order_release);
            if (xiaotai_tirtc_recovery_heap_leaked(
                    baseline, free_heap, OUTGOING_RETAINED_HEAP_LIMIT)) {
                atomic_store_explicit(&s_recovery_required, true,
                                      memory_order_release);
                BK_LOGE(TAG,
                        "TiRTC null-handle cleanup leaked heap baseline=%u current=%u retained=%u\n",
                        (unsigned)baseline, (unsigned)free_heap,
                        (unsigned)retained);
            } else {
                release_outgoing_connect();
                BK_LOGI(TAG,
                        "TiRTC null-handle cleanup recovered baseline=%u current=%u retained=%u\n",
                        (unsigned)baseline, (unsigned)free_heap,
                        (unsigned)retained);
            }
        }
    }
    return atomic_load_explicit(&s_recovery_required, memory_order_acquire);
}

bool xiaotai_tirtc_recovery_resources_restored(void)
{
    uint32_t baseline = (uint32_t)atomic_load_explicit(
        &s_outgoing_heap_baseline, memory_order_acquire);
    uint32_t free_heap = rtos_get_free_heap_size();
    uint32_t retained = baseline > free_heap ? baseline - free_heap : 0U;
    bool restored = baseline != 0U &&
        !xiaotai_tirtc_recovery_heap_leaked(
            baseline, free_heap, OUTGOING_RETAINED_HEAP_LIMIT);
    if (!restored) {
        BK_LOGE(TAG,
                "TiRTC runtime restart did not restore heap baseline=%u current=%u retained=%u\n",
                (unsigned)baseline, (unsigned)free_heap,
                (unsigned)retained);
    } else {
        BK_LOGI(TAG,
                "TiRTC runtime restart restored heap baseline=%u current=%u retained=%u\n",
                (unsigned)baseline, (unsigned)free_heap,
                (unsigned)retained);
    }
    return restored;
}

void xiaotai_tirtc_log_stream_media_state(void)
{
    connection_mode_t mode = (connection_mode_t)atomic_load_explicit(
        &s_connection_mode, memory_order_acquire);
    BK_LOGI(TAG,
            "STREAM media state connected=%d mode=%d audio-running=%d "
            "audio-uplink-subscribed=%d video-running=%d "
            "video-uplink-subscribed=%d heap=%u psram=%u\n",
            xiaotai_tirtc_connected(), (int)mode, xiaotai_audio_running(),
            atomic_load_explicit(&s_audio_subscribed, memory_order_acquire),
            xiaotai_video_running(),
            atomic_load_explicit(&s_video_subscribed, memory_order_acquire),
            (unsigned)rtos_get_free_heap_size(),
            (unsigned)rtos_get_psram_free_heap_size());
}

uint32_t xiaotai_tirtc_generation(void)
{
    return (uint32_t)atomic_load_explicit(&s_active_generation,
                                          memory_order_acquire);
}

const char *xiaotai_tirtc_version(void)
{
    return TiRtcGetVersion();
}

int xiaotai_tirtc_set_log_level(int level)
{
    if (level < 0 || level > TIRTC_LOG_LEVEL_MAX) return BK_ERR_PARAM;
    TiRtcLogSetLevel(level);
    s_log_level = level;
    return BK_OK;
}

int xiaotai_tirtc_get_log_level(void)
{
    return s_log_level;
}
