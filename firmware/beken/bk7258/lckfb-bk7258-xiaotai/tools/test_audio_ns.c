#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include "xiaotai_audio_ns.h"
#include <modules/audio_ns.h>

static int s_init_rc;
static int s_deinit_rc;
static int s_suppression_rc;
static int s_process_rc;
static unsigned s_init_calls;
static unsigned s_deinit_calls;
static unsigned s_suppression_calls;
static unsigned s_process_calls;
static int s_frame_samples;
static int s_sample_rate_hz;
static int s_suppression_db;

static void reset_fake(void)
{
    s_init_rc = BK_OK;
    s_deinit_rc = BK_OK;
    s_suppression_rc = BK_OK;
    s_process_rc = 1;
    s_init_calls = 0U;
    s_deinit_calls = 0U;
    s_suppression_calls = 0U;
    s_process_calls = 0U;
    s_frame_samples = 0;
    s_sample_rate_hz = 0;
    s_suppression_db = 0;
}

int bk_aud_ns_init(int frame_size_20ms, int sample_rate_hz)
{
    ++s_init_calls;
    s_frame_samples = frame_size_20ms;
    s_sample_rate_hz = sample_rate_hz;
    return s_init_rc;
}

int bk_aud_ns_deinit(void)
{
    ++s_deinit_calls;
    return s_deinit_rc;
}

int bk_aud_ns_process(int16_t *samples)
{
    assert(samples != NULL);
    ++s_process_calls;
    samples[0] /= 2;
    return s_process_rc;
}

int bk_aud_ns_set_suprs(int value)
{
    ++s_suppression_calls;
    s_suppression_db = value;
    return s_suppression_rc;
}

int main(void)
{
    xiaotai_audio_ns_t ns = {0};
    reset_fake();
    assert(xiaotai_audio_ns_start(&ns, 16000U, 320U, -25));
    assert(s_init_calls == 1U);
    assert(s_frame_samples == 320);
    assert(s_sample_rate_hz == 16000);
    assert(s_suppression_calls == 1U);
    assert(s_suppression_db == -25);
    assert(xiaotai_audio_ns_ready(&ns));

    int16_t samples[320] = {100, -200};
    bool speech = false;
    assert(xiaotai_audio_ns_process(&ns, samples, 320U, &speech));
    assert(s_process_calls == 1U);
    assert(speech);
    assert(samples[0] == 50);

    s_process_rc = 0;
    speech = true;
    assert(xiaotai_audio_ns_process(&ns, samples, 320U, &speech));
    assert(!speech);
    assert(!xiaotai_audio_ns_process(&ns, samples, 160U, &speech));
    assert(s_process_calls == 2U);

    s_process_rc = BK_FAIL;
    assert(!xiaotai_audio_ns_process(&ns, samples, 320U, &speech));
    xiaotai_audio_ns_stop(&ns);
    assert(s_deinit_calls == 1U);
    assert(!xiaotai_audio_ns_ready(&ns));

    reset_fake();
    memset(&ns, 0, sizeof(ns));
    assert(!xiaotai_audio_ns_start(&ns, 8000U, 160U, -25));
    assert(s_init_calls == 0U);

    reset_fake();
    s_suppression_rc = BK_FAIL;
    memset(&ns, 0, sizeof(ns));
    assert(!xiaotai_audio_ns_start(&ns, 16000U, 320U, -25));
    assert(s_deinit_calls == 1U);
    return 0;
}
