#pragma once

/*
 * Product media tuning.
 *
 * Keep values that are adjusted during camera, transport, and call tuning in
 * this file. Kconfig is reserved for build composition and hardware feature
 * switches; generated sdkconfig files must not become the source of truth for
 * runtime media policy.
 */

/* Normal RTC/IPC uplink profile. */
#define APP_MEDIA_CAMERA_CAPTURE_WIDTH                  1280U
#define APP_MEDIA_CAMERA_CAPTURE_HEIGHT                 960U
#define APP_MEDIA_RTC_VIDEO_WIDTH                       1280U
#define APP_MEDIA_RTC_VIDEO_HEIGHT                      960U
#define APP_MEDIA_RTC_H264_BITRATE_BPS                  4000000U
#define APP_MEDIA_RTC_H264_FPS                          20U
#define APP_MEDIA_RTC_H264_MIN_QP                       30U
#define APP_MEDIA_RTC_H264_MAX_QP                       45U

/*
 * Stable full-duplex device-call profile.
 *
 * The encoder bitrate is a rate-control target, not a hard ceiling. Device
 * calls use a VGA 4:3 surface independently from the portrait WeChat profile.
 * The receiver converts it once to the panel viewport through PPA. Reserve
 * 1.2 Mbit/s so the higher pixel count does not regress into the soft output
 * observed with the former 384x256 / 512 kbit/s profile. Weak-network
 * adaptation may lower this target only after transport feedback.
 */
#define APP_MEDIA_CALL_VIDEO_WIDTH                      640U
#define APP_MEDIA_CALL_VIDEO_HEIGHT                     480U
#define APP_MEDIA_CALL_VIDEO_FPS                        12U
#define APP_MEDIA_CALL_VIDEO_BITRATE_BPS                1200000U
#define APP_MEDIA_CALL_VIDEO_MIN_QP                     28U
#define APP_MEDIA_CALL_VIDEO_MAX_QP                     44U

/* XiaoTai phone/browser uplink: balanced full-duplex sensor-orientation
 * profile. Keep the full 4:3 field of view and scale it to 75%, but leave the
 * pixels unrotated. The receiver applies the reported clockwise 270-degree
 * camera rotation. */
#define APP_MEDIA_WECHAT_VIDEO_WIDTH                    960U
#define APP_MEDIA_WECHAT_VIDEO_HEIGHT                   720U
#define APP_MEDIA_WECHAT_VIDEO_FPS                      12U
#define APP_MEDIA_WECHAT_VIDEO_BITRATE_BPS              1500000U
#define APP_MEDIA_WECHAT_VIDEO_MIN_QP                   30U
#define APP_MEDIA_WECHAT_VIDEO_MAX_QP                   46U

/* H5 remote view encodes the native 1280x960 sensor surface directly. The
 * receiver applies the reported clockwise 270-degree camera rotation, which
 * avoids a blocking full-frame PPA pass on every frame. Keep the 20 fps cadence
 * while limiting the target to 3 Mbit/s so transport pressure does not turn
 * into non-key-frame suppression. */
#define APP_MEDIA_H5_VIDEO_WIDTH                        APP_MEDIA_RTC_VIDEO_WIDTH
#define APP_MEDIA_H5_VIDEO_HEIGHT                       APP_MEDIA_RTC_VIDEO_HEIGHT
#define APP_MEDIA_H5_VIDEO_FPS                          APP_MEDIA_RTC_H264_FPS
#define APP_MEDIA_H5_VIDEO_BITRATE_BPS                  3000000U
#define APP_MEDIA_H5_VIDEO_MIN_QP                       APP_MEDIA_RTC_H264_MIN_QP
#define APP_MEDIA_H5_VIDEO_MAX_QP                       APP_MEDIA_RTC_H264_MAX_QP
/* H264 encoder and transport protection. */
/*
 * IPC and full-duplex calls both keep a two-second recovery interval. A short
 * call GOP bounds decoder recovery after transport loss and prevents a long
 * run of unusable delta frames from filling the shared audio/video send queue.
 * Stream start, subscription, transport recovery, and peer requests can still
 * force an earlier IDR.
 */
#define APP_MEDIA_H264_GOP_DURATION_MS                  2000U
#define APP_MEDIA_CALL_H264_GOP_DURATION_MS             2000U
#define APP_MEDIA_H264_OUTPUT_BUFFER_BYTES              (1024U * 1024U)
#define APP_MEDIA_H264_MAX_DELTA_PAYLOAD_BYTES          (256U * 1024U)
#define APP_MEDIA_H264_STARTUP_GUARD_MS                 2500U
#define APP_MEDIA_H264_STARTUP_MAX_DELTA_PAYLOAD_BYTES  (128U * 1024U)
#define APP_MEDIA_H264_KEY_FRAME_REQUEST_MIN_INTERVAL_MS 2000U
#define APP_MEDIA_TRANSPORT_BACKPRESSURE_HOLD_MS        80U

/* Local pressure fallback. Disabled builds do not execute this policy. */
#define APP_MEDIA_AUTO_DEGRADE_SAMPLES                  2U
#define APP_MEDIA_AUTO_RECOVER_SAMPLES                  6U
#define APP_MEDIA_AUTO_COOLDOWN_MS                      10000U
#define APP_MEDIA_AUTO_PRESSURE_BUFFER_PCT              8U
#define APP_MEDIA_AUTO_SEVERE_BUFFER_PCT                25U
#define APP_MEDIA_AUTO_HEALTHY_BUFFER_PCT               2U
#define APP_MEDIA_AUTO_PRESSURE_QUEUE_DEPTH             2U
#define APP_MEDIA_AUTO_SEVERE_QUEUE_DEPTH               4U

/* TGMP bitrate controller hysteresis. */
#define APP_MEDIA_TGMP_EVENT_MIN_INTERVAL_US            500000ULL
#define APP_MEDIA_TGMP_EVENT_FAST_STEP_BPS              64000U
/* The lower bound is exposed only after transport feedback reports congestion.
 * VGA calls derive a 300 kbit/s floor from the normal 1.2 Mbit/s target; the
 * compact 96 kbit/s floor remains available to smaller profiles. */
#define APP_MEDIA_TGMP_COMPACT_MIN_BITRATE_BPS           (96U * 1000U)
#define APP_MEDIA_TGMP_LARGE_MIN_BITRATE_BPS            (750U * 1000U)
#define APP_MEDIA_TGMP_MIN_RATIO_DIVISOR                4U
#define APP_MEDIA_TGMP_START_RANGE_PERCENT              80U
#define APP_MEDIA_TGMP_MIN_STEP_BPS                     (16U * 1000U)
#define APP_MEDIA_TGMP_MIN_STEP_PERCENT                 10U
#define APP_MEDIA_TGMP_PROTECTION_INTERVAL_MS           1000U
#define APP_MEDIA_TGMP_EMERGENCY_DROP_PERCENT           25U
#define APP_MEDIA_TGMP_RECOVERY_HOLD_MS                 5000U
#define APP_MEDIA_TGMP_RECOVERY_INTERVAL_MS             3000U
#define APP_MEDIA_TGMP_RECOVERY_STEP_PERCENT            15U
#define APP_MEDIA_TGMP_RECOVERY_MIN_STEP_BPS            (64U * 1000U)

/* Focused diagnostics. Keep the initial trace count at zero for normal use. */
#define APP_MEDIA_CAMERA_FRAME_TRACE_INITIAL_COUNT      0U
#define APP_MEDIA_CAMERA_FRAME_TRACE_INTERVAL_MS        10000U

#if APP_MEDIA_CALL_VIDEO_MIN_QP > APP_MEDIA_CALL_VIDEO_MAX_QP
#error "Call video minimum QP must not exceed maximum QP"
#endif

#if APP_MEDIA_CALL_VIDEO_FPS == 0U || APP_MEDIA_CALL_VIDEO_FPS > 15U
#error "Stable call profile must stay within 1-15 fps"
#endif

#if APP_MEDIA_RTC_H264_MIN_QP > APP_MEDIA_RTC_H264_MAX_QP
#error "RTC video minimum QP must not exceed maximum QP"
#endif

#if (APP_MEDIA_CALL_VIDEO_WIDTH % 16U) != 0U || \
    (APP_MEDIA_CALL_VIDEO_HEIGHT % 16U) != 0U
#error "Call video dimensions must be aligned to 16 pixels"
#endif

#if (APP_MEDIA_WECHAT_VIDEO_WIDTH % 16U) != 0U || \
    (APP_MEDIA_WECHAT_VIDEO_HEIGHT % 16U) != 0U
#error "WeChat video dimensions must be aligned to 16 pixels"
#endif

#if (APP_MEDIA_H5_VIDEO_WIDTH % 16U) != 0U || \
    (APP_MEDIA_H5_VIDEO_HEIGHT % 16U) != 0U
#error "H5 video dimensions must be aligned to 16 pixels"
#endif

#if APP_MEDIA_TGMP_COMPACT_MIN_BITRATE_BPS > APP_MEDIA_CALL_VIDEO_BITRATE_BPS
#error "TGMP compact floor must not exceed the normal call bitrate"
#endif

#if APP_MEDIA_TGMP_START_RANGE_PERCENT == 0U || \
    APP_MEDIA_TGMP_START_RANGE_PERCENT >= 100U
#error "TGMP start range percentage must stay strictly between 0 and 100"
#endif
