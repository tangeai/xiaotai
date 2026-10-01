#include "xiaotai_audio_agc.h"

#include <limits.h>
#include <string.h>

#include <modules/audio_agc.h>

#define AGC_MIN_SAMPLE_RATE_HZ 8000U
#define AGC_MAX_SAMPLE_RATE_HZ 16000U
#define AGC_FRAME_DURATION_HZ 50U
#define AGC_MAX_FRAME_SAMPLES 320U
#define AGC_MIN_LEVEL 0
#define AGC_MAX_LEVEL 255
#define AGC_COMPRESSION_GAIN_DB 16
#define AGC_TARGET_LEVEL_DBFS 3

static uint32_t sample_abs(int16_t sample)
{
    return sample == INT16_MIN ? 32768U :
           (uint32_t)(sample < 0 ? -sample : sample);
}

bool xiaotai_audio_agc_start(xiaotai_audio_agc_t *agc,
                             uint32_t sample_rate_hz,
                             size_t frame_samples)
{
    if (agc == NULL ||
        (sample_rate_hz != AGC_MIN_SAMPLE_RATE_HZ &&
         sample_rate_hz != AGC_MAX_SAMPLE_RATE_HZ) ||
        frame_samples != sample_rate_hz / AGC_FRAME_DURATION_HZ) return false;
    memset(agc, 0, sizeof(*agc));
    if (bk_aud_agc_create(&agc->instance) != BK_OK ||
        agc->instance == NULL ||
        bk_aud_agc_init(agc->instance, AGC_MIN_LEVEL, AGC_MAX_LEVEL,
                        (int)sample_rate_hz) != BK_OK) {
        xiaotai_audio_agc_stop(agc);
        return false;
    }
    const bk_agc_config_t config = {
        .compressionGaindB = AGC_COMPRESSION_GAIN_DB,
        .limiterEnable = 1,
        .targetLevelDbfs = AGC_TARGET_LEVEL_DBFS,
    };
    if (bk_aud_agc_set_config(agc->instance, config) != BK_OK) {
        xiaotai_audio_agc_stop(agc);
        return false;
    }
    agc->sample_rate_hz = sample_rate_hz;
    agc->frame_samples = frame_samples;
    return true;
}

bool xiaotai_audio_agc_process(xiaotai_audio_agc_t *agc,
                               int16_t *samples,
                               size_t sample_count,
                               xiaotai_audio_agc_stats_t *stats)
{
    if (stats != NULL) memset(stats, 0, sizeof(*stats));
    if (!xiaotai_audio_agc_ready(agc) || samples == NULL ||
        sample_count != agc->frame_samples) return false;

    int16_t output[AGC_MAX_FRAME_SAMPLES];
    xiaotai_audio_agc_stats_t current = {0};
    for (size_t i = 0; i < sample_count; ++i) {
        uint32_t peak = sample_abs(samples[i]);
        if (peak > current.input_peak) current.input_peak = peak;
    }
    if (bk_aud_agc_process(agc->instance, samples, (int16_t)sample_count,
                           output) != BK_OK) return false;
    for (size_t i = 0; i < sample_count; ++i) {
        uint32_t peak = sample_abs(output[i]);
        if (peak > current.output_peak) current.output_peak = peak;
        if (output[i] == INT16_MAX || output[i] == INT16_MIN) {
            ++current.limited_samples;
        }
    }
    memcpy(samples, output, sample_count * sizeof(*samples));
    if (stats != NULL) *stats = current;
    return true;
}

bool xiaotai_audio_agc_ready(const xiaotai_audio_agc_t *agc)
{
    return agc != NULL && agc->instance != NULL &&
           (agc->sample_rate_hz == AGC_MIN_SAMPLE_RATE_HZ ||
            agc->sample_rate_hz == AGC_MAX_SAMPLE_RATE_HZ) &&
           agc->frame_samples ==
               agc->sample_rate_hz / AGC_FRAME_DURATION_HZ;
}

void xiaotai_audio_agc_stop(xiaotai_audio_agc_t *agc)
{
    if (agc == NULL) return;
    if (agc->instance != NULL) bk_aud_agc_free(agc->instance);
    memset(agc, 0, sizeof(*agc));
}
