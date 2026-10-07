#pragma once

#include "media_tuning.h"

/*
 * P4 downlink decode and presentation scheduling.
 *
 * These values describe one tightly coupled pipeline. Keep them together so a
 * scheduling experiment cannot silently change only the caller or only the
 * TinyH264 helper. They are application policy, not sdkconfig options.
 */

#define CALL_VIDEO_DECODE_MAX_WIDTH         APP_MEDIA_CALL_VIDEO_WIDTH
#define CALL_VIDEO_DECODE_MAX_HEIGHT        APP_MEDIA_CALL_VIDEO_HEIGHT
/*
 * Fill the 480px panel height with a centered 4:3 video canvas. Device-call
 * 640x480 frames fit without geometric scaling; rotated frames are contained.
 * Controls remain in the foreground above the video image.
 */
#define CALL_VIDEO_RENDER_WIDTH             640U
#define CALL_VIDEO_RENDER_HEIGHT            480U
#define CALL_VIDEO_REQUIRE_MATCHING_ASPECT   0U
#define CALL_VIDEO_H264_FIT_COVER            0U
#define CALL_VIDEO_MJPEG_ADAPTIVE_PLAYOUT    0U

#define CALL_VIDEO_INPUT_SLOT_COUNT         24U
#define CALL_VIDEO_INPUT_SLOT_CAPACITY      (256U * 1024U)
#define CALL_VIDEO_SOURCE_CROP_X            0U
#define CALL_VIDEO_SOURCE_CROP_Y            0U
#define CALL_VIDEO_SOURCE_CROP_WIDTH        APP_MEDIA_CALL_VIDEO_WIDTH
#define CALL_VIDEO_SOURCE_CROP_HEIGHT       APP_MEDIA_CALL_VIDEO_HEIGHT
#define CALL_VIDEO_DECODED_SLOT_COUNT       4U
/*
 * Ten retained RGB slots keep the weak-network burst reserve bounded. At the
 * 640x480 viewport each slot is 600 KiB (6000 KiB total).
 */
#define CALL_VIDEO_OUTPUT_SLOT_COUNT        10U

#define CALL_VIDEO_TASK_STACK_SIZE          (16U * 1024U)
#define CALL_VIDEO_MJPEG_TASK_STACK_SIZE    (8U * 1024U)
#define CALL_VIDEO_CONVERT_TASK_STACK_SIZE  (8U * 1024U)
#define CALL_VIDEO_INGRESS_TASK_STACK_SIZE  (3U * 1024U)

/*
 * Full-duplex calls must leave CPU0 scheduling windows for TiRTC transport.
 * The lower-priority compressed ingress relay returns the SDK callback before
 * waking this worker, so TinyH264 may keep its real-time priority without
 * running inside the socket call chain. Capture audio stays above the video
 * workers. Conversion and LVGL stay on CPU1, while single-owner TinyH264 is
 * SMP-migratable below both UI/audio and network/control deadlines. It consumes
 * whichever CPU is idle without trapping the decoder behind one busy owner.
 * The helper settings are retained for controlled experiments, but the product
 * path stays single-task.
 */
#define CALL_VIDEO_TASK_PRIORITY             5U
#define CALL_VIDEO_MJPEG_TASK_PRIORITY       6U
#define CALL_VIDEO_INGRESS_TASK_PRIORITY      4U
/* The P4 TinyH264 dual-task path can race inside its slice/deblock worker.
 * Dynamic 480x320 calls reproduced a load fault in GetChromaEdgeThresholds ->
 * h264bsdFilterPictureOneBlock -> filterThread even after lost phase
 * notifications were guarded. Keep one decoder owner until the prebuilt
 * TinyH264 implementation itself is fixed. */
#define CALL_VIDEO_H264_DUAL_TASK_ENABLE      0U
#define CALL_VIDEO_H264_HELPER_TASK_CORE      1U
#define CALL_VIDEO_H264_HELPER_TASK_PRIORITY 5U
#define CALL_VIDEO_CONVERT_TASK_PRIORITY      4U

#define CALL_VIDEO_START_TIMEOUT_MS           5000U
#define CALL_VIDEO_STOP_TIMEOUT_MS            3000U
#define CALL_VIDEO_MJPEG_DECODE_TIMEOUT_MS     100U
#define CALL_VIDEO_STATS_INTERVAL_US         (10LL * 1000LL * 1000LL)
/* The first-frame and focused stall logs are sufficient in normal firmware.
 * Per-frame boot traces are long synchronous UART writes and can themselves
 * perturb the exact startup interval being measured. Set this temporarily to
 * a small non-zero value only for a dedicated pipeline timing capture. */
#define CALL_VIDEO_STARTUP_TRACE_FRAMES        0U
#define CALL_VIDEO_TARGET_FRAME_INTERVAL_US    \
    ((1000000U + APP_MEDIA_CALL_VIDEO_FPS - 1U) / APP_MEDIA_CALL_VIDEO_FPS)
#define CALL_VIDEO_SLOW_DECODE_US              (CALL_VIDEO_TARGET_FRAME_INTERVAL_US * 2U)
#define CALL_VIDEO_INPUT_GAP_US                (CALL_VIDEO_TARGET_FRAME_INTERVAL_US * 3U)
/*
 * High-motion streams and TGTRP retransmission bursts can briefly lift the
 * compressed queue even though the following windows catch up to the source
 * source. Rebuilding TinyH264 for that recoverable burst creates the periodic
 * freeze it was meant to cure. Recover only after a backlog stays above twelve
 * frames for about half a second. The 24-slot input pool covers the measured
 * 1.3-2.1 second retransmission bursts while exhaustion remains an immediate
 * hard recovery signal.
 */
#define CALL_VIDEO_LATENCY_RECOVERY_US          (CALL_VIDEO_TARGET_FRAME_INTERVAL_US * 12U)
#define CALL_VIDEO_LATENCY_RECOVERY_DEPTH       12U
#define CALL_VIDEO_LATENCY_RECOVERY_SAMPLES     8U
#define CALL_VIDEO_LATENCY_RECOVERY_CLEAR_US    (CALL_VIDEO_TARGET_FRAME_INTERVAL_US * 6U)
#define CALL_VIDEO_ADAPTIVE_PLAYOUT_GAP_US      (CALL_VIDEO_TARGET_FRAME_INTERVAL_US * 4U)
#define CALL_VIDEO_ADAPTIVE_IMMEDIATE_GAP_US    (CALL_VIDEO_TARGET_FRAME_INTERVAL_US * 6U)
#define CALL_VIDEO_ADAPTIVE_CONFIRM_WINDOW_US   (2LL * 1000LL * 1000LL)
#define CALL_VIDEO_ADAPTIVE_CONFIRM_SAMPLES     2U
#define CALL_VIDEO_ADAPTIVE_PLAYOUT_HOLD_US     (8LL * 1000LL * 1000LL)
#define CALL_VIDEO_ADAPTIVE_PLAYOUT_DEPTH       2U
/* Waveshare 4.3-inch: the 640x384 RGB reservoir holds ten slots; keep the weak-network
 * playout reserve below it while preserving the LCD-retained slot. */
#define CALL_VIDEO_ADAPTIVE_PLAYOUT_MAX_DEPTH   8U
_Static_assert(CALL_VIDEO_INPUT_SLOT_COUNT >=
                   CALL_VIDEO_ADAPTIVE_PLAYOUT_MAX_DEPTH + 2U,
               "compressed input pool must absorb the maximum playout reserve");
/* Keep the normal playout clock tied to the negotiated source cadence. The
 * stable deadline intentionally alternates across the LVGL timer quantum
 * instead of accumulating one timer tick of drift per frame. */
#define CALL_VIDEO_ADAPTIVE_PLAYOUT_INTERVAL_US CALL_VIDEO_TARGET_FRAME_INTERVAL_US
/* A weak-network reservoir can only refill when presentation is slower than
 * the active source. These rates used to be fixed at 14/12 fps for a 15 fps
 * stream. Derive the recovery cadence from the source so normal playout stays
 * unchanged while a confirmed retransmission burst temporarily refills. */
#define CALL_VIDEO_ADAPTIVE_REFILL_FPS            \
    ((APP_MEDIA_CALL_VIDEO_FPS > 1U) ? (APP_MEDIA_CALL_VIDEO_FPS - 1U) : 1U)
#define CALL_VIDEO_ADAPTIVE_CRITICAL_FPS          \
    ((APP_MEDIA_CALL_VIDEO_FPS > 3U) ? (APP_MEDIA_CALL_VIDEO_FPS - 3U) : 1U)
#define CALL_VIDEO_ADAPTIVE_REFILL_INTERVAL_US    \
    ((1000000U + CALL_VIDEO_ADAPTIVE_REFILL_FPS - 1U) / \
     CALL_VIDEO_ADAPTIVE_REFILL_FPS)
#define CALL_VIDEO_ADAPTIVE_CRITICAL_INTERVAL_US  \
    ((1000000U + CALL_VIDEO_ADAPTIVE_CRITICAL_FPS - 1U) / \
     CALL_VIDEO_ADAPTIVE_CRITICAL_FPS)
/* The short window applies only after the compressed input queue is empty. RTC
 * input has a higher priority than the decoder, so delaying every backlogged
 * access unit merely accumulates latency on high-motion frames. The longer
 * periodic window still applies with backlog: it gives IDLE0 one deterministic
 * opportunity per second and stays well inside the five-second TWDT deadline. */
#define CALL_VIDEO_DECODE_SCHEDULING_WINDOW_MS      1U
#define CALL_VIDEO_DECODE_IDLE_WINDOW_MS            5U
#define CALL_VIDEO_DECODE_IDLE_WINDOW_EVERY_FRAMES 30U
#define CALL_VIDEO_DECODE_HANG_TIMEOUT_US       (2LL * 1000LL * 1000LL)
#define CALL_VIDEO_STALL_LOG_INTERVAL_US       (5LL * 1000LL * 1000LL)
