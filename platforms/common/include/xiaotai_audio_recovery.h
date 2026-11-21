#ifndef XIAOTAI_AUDIO_RECOVERY_H
#define XIAOTAI_AUDIO_RECOVERY_H

#include <stdbool.h>
#include <stdint.h>

/* The sender's millisecond clock is used only to identify interior media
 * gaps, never as a wall-clock deadline. All state belongs to the decoder
 * worker. Epochs must advance at explicit round/session boundaries. */
typedef struct {
    uint32_t media_ms, rx_ms, epoch;
    uint16_t samples;
    uint8_t stream;
} xiaotai_audio_recovery_stamp_t;

typedef struct {
    xiaotai_audio_recovery_stamp_t last;
    uint32_t next_media_ms;
    uint8_t steady_intervals;
    bool valid, desync;
} xiaotai_audio_recovery_t;

typedef struct {
    bool discard, reset, regular;
    uint8_t missing_frames;
} xiaotai_audio_recovery_plan_t;

/* Half-range serial arithmetic: within one epoch media spans must be less
 * than 2^31 ms. Natural uint32 rollover is not a discontinuity. */
static inline bool xiaotai_audio_recovery_before(uint32_t a, uint32_t b)
{
    return (uint32_t)(a - b) >= UINT32_C(0x80000000);
}

static inline xiaotai_audio_recovery_plan_t xiaotai_audio_recovery_plan(
    const xiaotai_audio_recovery_t *state,
    const xiaotai_audio_recovery_stamp_t *stamp, bool recovery_capable,
    bool fixed_voice_frame)
{
    xiaotai_audio_recovery_plan_t plan = {0};
    if (!state->valid) {
        plan.reset = true;
        return plan;
    }
    if (stamp->epoch != state->last.epoch) {
        plan.discard = xiaotai_audio_recovery_before(stamp->epoch, state->last.epoch);
        plan.reset = !plan.discard;
        return plan;
    }
    if (stamp->stream != state->last.stream) {
        /* AI admits one publisher per epoch. An interleaved foreign stream
         * must not reset the selected stream's consumed high-water mark. */
        plan.discard = true;
        return plan;
    }
    if (xiaotai_audio_recovery_before(stamp->media_ms, state->next_media_ms)) {
        plan.discard = true;
        return plan;
    }
    plan.reset = state->desync;
    bool continuous = !state->desync && fixed_voice_frame &&
        state->last.samples == 320 && stamp->samples == 320 &&
        stamp->rx_ms - state->last.rx_ms <= 120;
    uint32_t gap = stamp->media_ms - state->next_media_ms;
    plan.regular = continuous && gap == 0;
    /* Cadence is a conservative heuristic, not evidence of network loss:
     * DTX, timestamp resets and unknown sender pauses require boundaries. */
    if (recovery_capable && continuous && state->steady_intervals == 2 &&
        (gap == 20 || gap == 40))
        plan.missing_frames = (uint8_t)(gap / 20);
    return plan;
}

static inline void xiaotai_audio_recovery_begin(xiaotai_audio_recovery_t *state,
    const xiaotai_audio_recovery_stamp_t *stamp)
{
    *state = (xiaotai_audio_recovery_t){.last = *stamp,
        .next_media_ms = stamp->media_ms, .valid = true};
    state->last.samples = 0;
}

/* Commit each decoder-consumed subframe, independently of sink acceptance. */
static inline void xiaotai_audio_recovery_commit_missing(xiaotai_audio_recovery_t *state)
{
    state->next_media_ms += 20;
}

static inline void xiaotai_audio_recovery_commit_packet(xiaotai_audio_recovery_t *state,
    const xiaotai_audio_recovery_stamp_t *stamp,
    xiaotai_audio_recovery_plan_t plan, bool fixed_voice_frame)
{
    if (plan.regular) {
        if (state->steady_intervals < 2) ++state->steady_intervals;
    } else if (!plan.missing_frames) {
        state->steady_intervals = 0;
    }
    state->last = *stamp;
    state->next_media_ms = stamp->media_ms + stamp->samples / 16U;
    state->valid = true;
    state->desync = false;
    if (!fixed_voice_frame || stamp->samples != 320) {
        state->last.samples = 0;
        state->steady_intervals = 0;
    }
}

#endif
