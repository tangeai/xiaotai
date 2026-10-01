#ifndef STARTER_AEC_H
#define STARTER_AEC_H

#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"
#include "starter_signal_level.h"

#ifdef __cplusplus
extern "C" {
#endif

enum {
    STARTER_AEC_SAMPLE_RATE_HZ = 16000,
    STARTER_AEC_TRANSPORT_RATE_HZ = 8000,
    /* Board media adapters normalize hardware slots into this compact pair. */
    STARTER_AEC_CAPTURE_DMA_CHANNELS = 2,
    STARTER_AEC_MIC_SLOT = 0,
    STARTER_AEC_REFERENCE_SLOT = 1,
};

typedef struct {
    /** AEC 后、降采样前的 16 kHz clean PCM；仅在下一次 process 前有效。 */
    const int16_t *pcm_16k;
    size_t samples_16k;
    const int16_t *pcm_8k;
    size_t samples;
    uint32_t mic_clipped;
    uint32_t reference_clipped;
    starter_signal_level_t level[3]; /**< [DEBUG-wake49] mic/ref/clean, same frame. */
    uint32_t measurement_us;
} starter_aec_output_t;

/** 创建常驻的 ESP-SR 全双工 AEC，并在 PSRAM 中分配其状态和工作帧。 */
esp_err_t starter_aec_init(void);

/** 返回一次 I2S 读取必须填满的双通道压紧 DMA 字节数。 */
size_t starter_aec_capture_bytes(void);

/** 返回供板级采集直接写入的、16 字节对齐的双通道缓冲区。 */
int16_t *starter_aec_capture_buffer(void);

/**
 * 将归一化的麦克风（槽 0）和回放参考（槽 1）送入 AEC，同时返回只读的
 * 16 kHz clean PCM 给本地识别，并降采样到 8 kHz 给 TiRTC。
 * capture_bytes 必须等于 starter_aec_capture_bytes()。
 */
esp_err_t starter_aec_process_capture(size_t capture_bytes,
                                      starter_aec_output_t *output);

/** ESP-SR AEC 的 16 kHz 帧采样数，用于启动日志和实机诊断。 */
size_t starter_aec_frame_samples(void);

#ifdef __cplusplus
}
#endif

#endif
