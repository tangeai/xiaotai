#include <assert.h>
#include <limits.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include "xiaotai_audio_agc.h"
#include <modules/audio_agc.h>

static int s_instance;
static int s_create_rc;
static int s_init_rc;
static int s_config_rc;
static int s_process_rc;
static unsigned s_create_calls;
static unsigned s_init_calls;
static unsigned s_config_calls;
static unsigned s_process_calls;
static unsigned s_free_calls;
static int32_t s_min_level;
static int32_t s_max_level;
static uint32_t s_sample_rate_hz;
static int16_t s_process_samples;
static bk_agc_config_t s_config;

static void reset_fake(void)
{
    s_create_rc = BK_OK;
    s_init_rc = BK_OK;
    s_config_rc = BK_OK;
    s_process_rc = BK_OK;
    s_create_calls = 0U;
    s_init_calls = 0U;
    s_config_calls = 0U;
    s_process_calls = 0U;
    s_free_calls = 0U;
    s_min_level = -1;
    s_max_level = -1;
    s_sample_rate_hz = 0U;
    s_process_samples = 0;
    memset(&s_config, 0, sizeof(s_config));
}

int bk_aud_agc_create(void **instance)
{
    ++s_create_calls;
    if (s_create_rc == BK_OK) *instance = &s_instance;
    return s_create_rc;
}

int bk_aud_agc_init(void *instance, int32_t min_level, int32_t max_level,
                    uint32_t sample_rate_hz)
{
    assert(instance == &s_instance);
    ++s_init_calls;
    s_min_level = min_level;
    s_max_level = max_level;
    s_sample_rate_hz = sample_rate_hz;
    return s_init_rc;
}

int bk_aud_agc_set_config(void *instance, bk_agc_config_t config)
{
    assert(instance == &s_instance);
    ++s_config_calls;
    s_config = config;
    return s_config_rc;
}

int bk_aud_agc_process(void *instance, const int16_t *input,
                       int16_t samples, int16_t *output)
{
    assert(instance == &s_instance);
    ++s_process_calls;
    s_process_samples = samples;
    if (s_process_rc != BK_OK) return s_process_rc;
    for (int16_t i = 0; i < samples; ++i) {
        int32_t value = (int32_t)input[i] * 2;
        if (value > INT16_MAX) value = INT16_MAX;
        if (value < INT16_MIN) value = INT16_MIN;
        output[i] = (int16_t)value;
    }
    return BK_OK;
}

int bk_aud_agc_free(void *instance)
{
    assert(instance == &s_instance);
    ++s_free_calls;
    return BK_OK;
}

int main(void)
{
    xiaotai_audio_agc_t agc = {0};
    reset_fake();
    assert(xiaotai_audio_agc_start(&agc, 8000U, 160U));
    assert(s_create_calls == 1U);
    assert(s_init_calls == 1U);
    assert(s_min_level == 0);
    assert(s_max_level == 255);
    assert(s_sample_rate_hz == 8000U);
    assert(s_config_calls == 1U);
    assert(s_config.compressionGaindB == 16);
    assert(s_config.targetLevelDbfs == 3);
    assert(s_config.limiterEnable == 1U);

    int16_t samples[160] = {100, -200, 17000, -17000};
    xiaotai_audio_agc_stats_t stats = {0};
    assert(xiaotai_audio_agc_process(&agc, samples, 160U, &stats));
    assert(s_process_calls == 1U);
    assert(s_process_samples == 160);
    assert(samples[0] == 200);
    assert(samples[1] == -400);
    assert(samples[2] == INT16_MAX);
    assert(samples[3] == INT16_MIN);
    assert(stats.input_peak == 17000U);
    assert(stats.output_peak == 32768U);
    assert(stats.limited_samples == 2U);
    assert(!xiaotai_audio_agc_process(&agc, samples, 80U, &stats));
    assert(s_process_calls == 1U);
    xiaotai_audio_agc_stop(&agc);
    assert(s_free_calls == 1U);

    reset_fake();
    memset(&agc, 0, sizeof(agc));
    assert(xiaotai_audio_agc_start(&agc, 16000U, 320U));
    assert(s_sample_rate_hz == 16000U);
    int16_t opus_samples[320] = {50, -75};
    assert(xiaotai_audio_agc_process(&agc, opus_samples, 320U, &stats));
    assert(s_process_samples == 320);
    assert(opus_samples[0] == 100);
    assert(opus_samples[1] == -150);
    xiaotai_audio_agc_stop(&agc);

    reset_fake();
    s_config_rc = BK_FAIL;
    memset(&agc, 0, sizeof(agc));
    assert(!xiaotai_audio_agc_start(&agc, 8000U, 160U));
    assert(s_free_calls == 1U);
    assert(!xiaotai_audio_agc_ready(&agc));
    return 0;
}
