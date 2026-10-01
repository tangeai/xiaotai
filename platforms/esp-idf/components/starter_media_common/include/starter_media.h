#ifndef STARTER_MEDIA_H
#define STARTER_MEDIA_H

/**
 * @file starter_media.h
 * @brief 产品音视频硬件的唯一接入点。
 *
 * 协议状态机和 TiRTC SDK 适配层不感知摄像头、codec、功放或 DMA 的板级
 * 细节；一个 generation 代表一次 TiRTC 连接，旧 generation 的下行帧会
 * 被丢弃。各板实现此契约，公共媒体算法只依赖归一化后的 PCM/帧语义。
 */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "driver/i2c_master.h"
#include "esp_err.h"
#include "starter_tirtc.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    bool active;                 /**< 当前会话是否允许媒体任务运行。 */
    starter_tirtc_mode_t mode;   /**< 当前是 H5 还是 AI；AI 只发送音频。 */
    uint32_t generation;         /**< 当前连接代次，用于过滤迟到帧。 */
    uint32_t audio_sent;         /**< 产品适配器成功发送的音频帧数。 */
    uint32_t video_sent;         /**< 产品适配器成功发送的视频帧数。 */
    uint32_t audio_received;     /**< 成功复制到播放队列的音频帧数。 */
    uint32_t audio_dropped;      /**< 参数无效、队列满或代次过期的帧数。 */
    uint32_t audio_decoded;      /**< 已成功从 A-law 解码的下行帧数。 */
    uint32_t audio_played;       /**< 已完整写入 I2S 播放 DMA 的下行帧数。 */
    uint32_t audio_decode_failed; /**< A-law 解码失败或空输出的帧数。 */
    uint32_t audio_playback_blocked; /**< 静音、功放或会话门禁拒绝的帧数。 */
    uint32_t audio_write_failed; /**< I2S 写入失败或部分写入的帧数。 */
    uint32_t aec_processed;      /**< 已完成的 16 kHz ESP-SR AEC 帧数。 */
    uint32_t aec_errors;         /**< AEC 输入帧不完整或处理边界错误数。 */
    uint32_t aec_mic_clipped;    /**< MIC1 接近满量程的累计采样数。 */
    uint32_t aec_reference_clipped; /**< MIC3 回放参考接近满量程的采样数。 */
    uint32_t aec_max_process_us; /**< 单帧 AEC 的最大处理耗时。 */
    uint32_t aec_deadline_misses; /**< AEC 处理超过 32 ms 输入周期的次数。 */
    uint32_t jpeg_max_encode_us; /**< 单帧软件 JPEG 的最大编码耗时。 */
    uint32_t jpeg_deadline_misses; /**< JPEG 编码超过 125 ms 目标周期的次数。 */
    uint8_t audio_activity_level; /**< 平滑后的环境声音强度，0..3，只表示声学活动。 */
    bool voice_active;          /**< 声音强度持续至少 300 ms 后的活动标志。 */
    uint8_t speaker_volume;     /**< 产品音量档位，0..10。 */
    bool speaker_muted;         /**< 全局扬声器静音。 */
    bool microphone_muted;      /**< 全局麦克风静音；AEC/活动检测仍继续运行。 */
    bool uplink_enabled;        /**< 业务上行门禁；多人对讲只在 PTT 按下时开启。 */
} starter_media_status_t;

/** 创建固定下行音频队列和播放任务；应在启动其他模块前调用。 */
esp_err_t starter_media_init(void);

/**
 * 返回唯一的板级 driver_ng I2C 总线。
 *
 * 触摸、摄像头和音频编解码器必须复用此句柄；不得为同一物理总线创建 legacy
 * I2C 驱动或第二个 driver_ng master。
 */
i2c_master_bus_handle_t starter_media_i2c_bus(void);

/** 选择板载 LCD；具体扩展 GPIO 和有效电平由板级实现决定。 */
esp_err_t starter_media_select_lcd(bool selected);

/** 产品设置。音量为 0..10，接口会同步更新板载 codec 和功放门禁。 */
esp_err_t starter_media_set_speaker_volume(uint8_t volume);
esp_err_t starter_media_set_speaker_muted(bool muted);
void starter_media_set_microphone_muted(bool muted);
void starter_media_set_uplink_enabled(bool enabled);
/** Runtime publishes foreground admission; microphone mute is also enforced. */
void starter_media_set_wake_allowed(bool allowed);
/** Prepare only for an acoustic wake; returns zero if stale, muted or busy. */
uint32_t starter_media_prepare_ai_preroll(int64_t captured_ms);
void starter_media_cancel_ai_preroll(uint32_t token);
/** Runtime-only transition: retain exactly this wake token while stopping H5. */
esp_err_t starter_media_stop_for_ai(uint32_t token);
/** Runtime polls overflow/discontinuity so a damaged utterance fails explicitly. */
bool starter_media_ai_preroll_failed(void);
typedef struct {
    uint32_t buffered_ms;
    bool owned;
    bool bound;
    bool failed;
} starter_media_preroll_status_t;
/** Nonblocking diagnostic snapshot. False means busy, not an empty cache. */
bool starter_media_preroll_status(starter_media_preroll_status_t *out);

/* [DEBUG-wake49] Last complete 32-frame observation window; mic/ref/clean order.
 * RMS values are average/max of frame RMS, DC is signed frame-mean average.
 * Readers do not reset counters or affect capture/recognition. */
typedef struct {
    uint32_t window;
    uint32_t at_ms;
    uint32_t frames;
    uint32_t rms_avg[3], rms_max[3], peak[3];
    int32_t dc_avg[3];
    uint32_t aec_avg_us, measurement_avg_us, measurement_max_us;
} starter_media_audio_diagnostics_t;
void starter_media_audio_diagnostics(starter_media_audio_diagnostics_t *out);

/**
 * 在没有 TiRTC 会话时同步播放 8 kHz 单声道 signed 16-bit PCM。
 * 数据由调用者持有且只在函数返回前访问；实现复用产品唯一的 I2S/功放路径。
 */
esp_err_t starter_media_play_pcm8k(const int16_t *pcm, size_t sample_count);
uint32_t starter_media_playback_epoch(void);
esp_err_t starter_media_play_pcm8k_at_epoch(const int16_t *pcm, size_t sample_count,
                                           uint32_t epoch);

/**
 * 请求中断当前提示音/铃声播放。通话媒体启动优先于非实时提示音；函数不等待
 * I2S DMA 完成，播放任务会在当前短 PCM 块结束后释放输出锁。
 */
void starter_media_cancel_pcm8k_playback(void);

/**
 * 允许指定连接代次开始媒体采集。
 *
 * 该函数由会话状态任务调用。板级实现应启动自己的采集/编码任务，但不要在
 * 此函数中无限阻塞。AI 模式不得启动视频采集。
 */
esp_err_t starter_media_start(starter_tirtc_mode_t mode, uint32_t generation);

/** 停止并回收板级采集任务；重复调用安全。 */
void starter_media_stop(void);

/** 让 MJPEG 采集任务尽快提交下一张完整 JPEG；过期 generation 会被忽略。 */
void starter_media_request_key_frame(uint32_t generation);

/**
 * 提交一帧下行 A-law 音频。
 *
 * 可从 SDK 回调调用：函数只做有界复制并以零等待时间投递固定队列；data 的
 * 所有权仍属于 SDK，函数返回后不会继续引用它。
 */
void starter_media_submit_audio(starter_tirtc_mode_t mode,
                                uint32_t generation,
                                const starter_tirtc_frame_t *frame,
                                const void *data);

/** 返回由原子变量组成的瞬时状态快照，可从任意任务调用。 */
starter_media_status_t starter_media_status(void);
#if CONFIG_IDF_TARGET_ESP32P4
void starter_media_set_call_video(bool video);
esp_err_t starter_media_set_camera_enabled(uint32_t generation, bool enabled);
void starter_media_submit_video(starter_tirtc_mode_t mode, uint32_t generation,
                                 const starter_tirtc_frame_t *frame, const void *data);
#endif

#ifdef __cplusplus
}
#endif

#endif
