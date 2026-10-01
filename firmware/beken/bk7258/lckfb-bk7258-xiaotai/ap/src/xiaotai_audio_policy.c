#include "xiaotai_audio_policy.h"

#include <limits.h>

static uint32_t sample_abs(int16_t sample)
{
    return sample == INT16_MIN ? 32768U :
           (uint32_t)(sample < 0 ? -sample : sample);
}

uint8_t xiaotai_audio_adc_gain_for_sensitivity(unsigned sensitivity)
{
    static const uint8_t gains[] = {0x25U, 0x29U, 0x2dU, 0x35U, 0x3fU};
    if (sensitivity < XIAOTAI_MIC_SENSITIVITY_MIN) {
        sensitivity = XIAOTAI_MIC_SENSITIVITY_MIN;
    } else if (sensitivity > XIAOTAI_MIC_SENSITIVITY_MAX) {
        sensitivity = XIAOTAI_MIC_SENSITIVITY_MAX;
    }
    return gains[sensitivity - XIAOTAI_MIC_SENSITIVITY_MIN];
}

bool xiaotai_audio_commit_sensitivity(
    uint8_t *current, unsigned requested,
    xiaotai_audio_apply_sensitivity_fn apply, void *context)
{
    if (current == NULL || apply == NULL ||
        requested < XIAOTAI_MIC_SENSITIVITY_MIN ||
        requested > XIAOTAI_MIC_SENSITIVITY_MAX ||
        apply(context, requested) != 0) {
        return false;
    }
    *current = (uint8_t)requested;
    return true;
}

bool xiaotai_audio_drop_report_due(uint32_t dropped_bytes,
                                   uint32_t *reported_bytes)
{
    if (reported_bytes == NULL || dropped_bytes == 0U ||
        dropped_bytes <= *reported_bytes) {
        return false;
    }
    if (*reported_bytes != 0U &&
        dropped_bytes - *reported_bytes <
            XIAOTAI_AUDIO_DROP_REPORT_INTERVAL_BYTES) {
        return false;
    }
    *reported_bytes = dropped_bytes;
    return true;
}

bool xiaotai_audio_aec_reference_active(const int16_t *reference,
                                        size_t samples)
{
    if (reference == NULL || samples == 0U) return false;
    for (size_t i = 0U; i < samples; ++i) {
        if (sample_abs(reference[i]) >= XIAOTAI_AEC_REFERENCE_ACTIVE_PEAK) {
            return true;
        }
    }
    return false;
}

bool xiaotai_audio_aec_profile_validated(uint32_t sample_rate_hz)
{
    return sample_rate_hz == 16000U;
}

size_t xiaotai_audio_frame_samples_for_rate(uint32_t sample_rate_hz)
{
    if (sample_rate_hz == 8000U) return XIAOTAI_AUDIO_ALAW_FRAME_SAMPLES;
    if (sample_rate_hz == 16000U) return XIAOTAI_AUDIO_OPUS_FRAME_SAMPLES;
    return 0U;
}

uint32_t xiaotai_audio_playback_tail_after_write(uint32_t current_tail_ms,
                                                  uint32_t now_ms,
                                                  uint32_t frame_duration_ms)
{
    uint32_t base = (int32_t)(current_tail_ms - now_ms) > 0 ?
                    current_tail_ms : now_ms;
    return base + frame_duration_ms;
}

bool xiaotai_audio_playback_drained(bool running, size_t queued_frames,
                                    bool frame_in_progress,
                                    uint32_t playback_tail_ms,
                                    uint32_t now_ms,
                                    uint32_t quiet_ms)
{
    if (!running) return true;
    if (queued_frames != 0U || frame_in_progress) return false;
    return (int32_t)(now_ms - playback_tail_ms) >= (int32_t)quiet_ms;
}
