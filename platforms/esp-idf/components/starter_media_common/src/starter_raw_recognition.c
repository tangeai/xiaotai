#include "starter_raw_recognition.h" /* Board code selects the physical mic slot. */
#include "starter_signal_level.h"

#include <string.h>

#define RECOGNITION_RAW_RMS_THRESHOLD 300U
#define RECOGNITION_RAW_HOLD_MS 300

bool starter_raw_recognition_feed(starter_raw_recognition_t *state,
                                  const int16_t *capture, size_t frames,
                                  size_t channels, size_t microphone_slot,
                                  int16_t *mono, size_t capacity,
                                  bool eligible, int64_t now_ms)
{
    if (state == NULL) return false;
    if (capture == NULL || mono == NULL || frames == 0 || frames > capacity ||
        channels == 0 || microphone_slot >= channels ||
        frames > SIZE_MAX / channels) {
        memset(state, 0, sizeof(*state));
        return false;
    }
    for (size_t i = 0; i < frames; ++i)
        mono[i] = capture[i * channels + microphone_slot];
    if (!eligible) {
        memset(state, 0, sizeof(*state));
        return false;
    }

    uint32_t rms = starter_signal_measure(mono, frames).rms;
    state->smoothed_rms = (state->smoothed_rms * 3U + rms) / 4U;
    bool candidate = state->smoothed_rms >= RECOGNITION_RAW_RMS_THRESHOLD;
    if (candidate != state->candidate_sound || now_ms < state->candidate_since_ms) {
        state->candidate_sound = candidate;
        state->candidate_since_ms = now_ms;
    } else if (now_ms - state->candidate_since_ms >= RECOGNITION_RAW_HOLD_MS) {
        state->sound = candidate;
    }
    return state->sound;
}
