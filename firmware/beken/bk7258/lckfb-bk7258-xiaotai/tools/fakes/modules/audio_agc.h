#ifndef MODULES_AUDIO_AGC_H
#define MODULES_AUDIO_AGC_H

#include <stdint.h>

#define BK_OK 0
#define BK_FAIL (-1)

typedef struct {
    int16_t targetLevelDbfs;
    int16_t compressionGaindB;
    uint8_t limiterEnable;
} bk_agc_config_t;

int bk_aud_agc_create(void **instance);
int bk_aud_agc_init(void *instance, int32_t min_level, int32_t max_level,
                    uint32_t sample_rate_hz);
int bk_aud_agc_set_config(void *instance, bk_agc_config_t config);
int bk_aud_agc_process(void *instance, const int16_t *input,
                       int16_t samples, int16_t *output);
int bk_aud_agc_free(void *instance);

#endif
