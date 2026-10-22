#ifndef XIAOTAI_AUDIO_AGC_H
#define XIAOTAI_AUDIO_AGC_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef struct {
    void *instance;
    uint32_t sample_rate_hz;
    size_t frame_samples;
} xiaotai_audio_agc_t;

typedef struct {
    uint32_t input_peak;
    uint32_t output_peak;
    uint32_t limited_samples;
} xiaotai_audio_agc_stats_t;

bool xiaotai_audio_agc_start(xiaotai_audio_agc_t *agc,
                             uint32_t sample_rate_hz,
                             size_t frame_samples);
bool xiaotai_audio_agc_process(xiaotai_audio_agc_t *agc,
                               int16_t *samples,
                               size_t sample_count,
                               xiaotai_audio_agc_stats_t *stats);
bool xiaotai_audio_agc_ready(const xiaotai_audio_agc_t *agc);
void xiaotai_audio_agc_stop(xiaotai_audio_agc_t *agc);

#endif
