#include <assert.h>
#include <stdint.h>

#include "xiaotai_audio_policy.h"

static int applied_level;
static int apply_result;

static int apply_sensitivity(void *context, unsigned sensitivity)
{
    (void)context;
    applied_level = (int)sensitivity;
    return apply_result;
}

int main(void)
{
    assert(xiaotai_audio_adc_gain_for_sensitivity(0U) == 0x25U);
    assert(xiaotai_audio_adc_gain_for_sensitivity(1U) == 0x25U);
    assert(xiaotai_audio_adc_gain_for_sensitivity(2U) == 0x29U);
    assert(xiaotai_audio_adc_gain_for_sensitivity(3U) == 0x2dU);
    assert(xiaotai_audio_adc_gain_for_sensitivity(4U) == 0x35U);
    assert(xiaotai_audio_adc_gain_for_sensitivity(5U) == 0x3fU);
    assert(xiaotai_audio_adc_gain_for_sensitivity(6U) == 0x3fU);

    uint8_t sensitivity = 4U;
    apply_result = -1;
    assert(!xiaotai_audio_commit_sensitivity(
        &sensitivity, 5U, apply_sensitivity, NULL));
    assert(applied_level == 5);
    assert(sensitivity == 4U);
    apply_result = 0;
    assert(xiaotai_audio_commit_sensitivity(
        &sensitivity, 5U, apply_sensitivity, NULL));
    assert(sensitivity == 5U);
    assert(!xiaotai_audio_commit_sensitivity(
        &sensitivity, 0U, apply_sensitivity, NULL));
    assert(sensitivity == 5U);

    assert(xiaotai_audio_frame_samples_for_rate(8000U) == 160U);
    assert(xiaotai_audio_frame_samples_for_rate(16000U) == 320U);
    assert(xiaotai_audio_frame_samples_for_rate(48000U) == 0U);

    uint32_t reported = 0U;
    assert(!xiaotai_audio_drop_report_due(0U, &reported));
    assert(xiaotai_audio_drop_report_due(160U, &reported));
    assert(reported == 160U);
    assert(!xiaotai_audio_drop_report_due(160U, &reported));
    assert(!xiaotai_audio_drop_report_due(160U + 10239U, &reported));
    assert(xiaotai_audio_drop_report_due(160U + 10240U, &reported));
    assert(reported == 10400U);

    int16_t reference[160] = {0};
    assert(!xiaotai_audio_aec_reference_active(NULL, 160U));
    assert(!xiaotai_audio_aec_reference_active(reference, 0U));
    reference[17] = 255;
    reference[83] = -255;
    assert(!xiaotai_audio_aec_reference_active(reference, 160U));
    reference[83] = -256;
    assert(xiaotai_audio_aec_reference_active(reference, 160U));

    /* Keep the repeatedly failing 8 kHz profile bypassed until the exact
     * artifact passes near-end and double-talk HIL. */
    assert(!xiaotai_audio_aec_profile_validated(8000U));
    assert(xiaotai_audio_aec_profile_validated(16000U));
    assert(!xiaotai_audio_aec_profile_validated(0U));

    uint32_t playback_tail = xiaotai_audio_playback_tail_after_write(
        0U, 1000U, 20U);
    assert(playback_tail == 1020U);
    playback_tail = xiaotai_audio_playback_tail_after_write(
        playback_tail, 1001U, 20U);
    assert(playback_tail == 1040U);
    assert(!xiaotai_audio_playback_drained(true, 0U, false,
                                           playback_tail, 1159U, 120U));
    assert(xiaotai_audio_playback_drained(true, 0U, false,
                                          playback_tail, 1160U, 120U));
    assert(xiaotai_audio_playback_tail_after_write(
               UINT32_MAX - 10U, UINT32_MAX - 20U, 20U) == 9U);

    assert(xiaotai_audio_playback_drained(false, 4U, true,
                                          1000U, 1000U, 120U));
    assert(!xiaotai_audio_playback_drained(true, 1U, false,
                                           1000U, 1200U, 120U));
    assert(!xiaotai_audio_playback_drained(true, 0U, true,
                                           1000U, 1200U, 120U));
    assert(!xiaotai_audio_playback_drained(true, 0U, false,
                                           1000U, 1119U, 120U));
    assert(xiaotai_audio_playback_drained(true, 0U, false,
                                          1000U, 1120U, 120U));
    assert(xiaotai_audio_playback_drained(true, 0U, false,
                                          UINT32_MAX - 60U, 59U, 120U));

    return 0;
}
