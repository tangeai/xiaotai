#ifndef XIAOTAI_AUDIO_POLICY_H
#define XIAOTAI_AUDIO_POLICY_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define XIAOTAI_AUDIO_DROP_REPORT_INTERVAL_BYTES 10240U
#define XIAOTAI_AEC_REFERENCE_ACTIVE_PEAK 256U
#define XIAOTAI_AUDIO_ALAW_FRAME_SAMPLES 160U
#define XIAOTAI_AUDIO_OPUS_FRAME_SAMPLES 320U
#define XIAOTAI_MIC_SENSITIVITY_MIN 1U
#define XIAOTAI_MIC_SENSITIVITY_DEFAULT 4U
#define XIAOTAI_MIC_SENSITIVITY_MAX 5U

typedef int (*xiaotai_audio_apply_sensitivity_fn)(void *context,
                                                  unsigned sensitivity);

/** Map the user-facing sensitivity level to the BK7258 ADC front-end gain. */
uint8_t xiaotai_audio_adc_gain_for_sensitivity(unsigned sensitivity);
/** Apply and commit a user setting only after the hardware accepts it. */
bool xiaotai_audio_commit_sensitivity(
    uint8_t *current, unsigned requested,
    xiaotai_audio_apply_sensitivity_fn apply, void *context);

bool xiaotai_audio_drop_report_due(uint32_t dropped_bytes,
                                   uint32_t *reported_bytes);
bool xiaotai_audio_aec_reference_active(const int16_t *reference,
                                        size_t samples);
bool xiaotai_audio_aec_profile_validated(uint32_t sample_rate_hz);
size_t xiaotai_audio_frame_samples_for_rate(uint32_t sample_rate_hz);
uint32_t xiaotai_audio_playback_tail_after_write(uint32_t current_tail_ms,
                                                  uint32_t now_ms,
                                                  uint32_t frame_duration_ms);
bool xiaotai_audio_playback_drained(bool running, size_t queued_frames,
                                    bool frame_in_progress,
                                    uint32_t playback_tail_ms,
                                    uint32_t now_ms,
                                    uint32_t quiet_ms);

#endif
