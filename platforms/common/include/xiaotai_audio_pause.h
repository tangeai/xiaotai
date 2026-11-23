#ifndef XIAOTAI_AUDIO_PAUSE_H
#define XIAOTAI_AUDIO_PAUSE_H

#include <stdbool.h>
#include <stdint.h>

#include "xiaotai_audio_recovery.h"

/* AI downlink sender pauses.
 *
 * The TTS sender omits silent frames, so its media clock leaves gaps between
 * words. A sink that writes packets back-to-back removes those gaps. This
 * planner turns a gap in the sender's media clock into the silence the sink
 * should write before the next packet. The media clock only places silence;
 * it is never a deadline and never changes buffering decisions.
 *
 * One timeline per epoch (a turn); the owner resets it at turn boundaries.
 * Packets that arrive late, duplicated or reordered never move the timeline
 * backwards. A large step back is a sender clock reset and restarts there.
 * Gaps are whole frames, capped at XIAOTAI_AUDIO_PAUSE_MAX_MS so a stall or a
 * clock jump cannot add unbounded delay.
 *
 * Boards that conceal short gaps in the decoder (recovery capable) must not
 * also insert silence for the gaps the decoder covers: pass the span the
 * decoder handles as concealed_ms so only longer gaps get silence. */
#define XIAOTAI_AUDIO_PAUSE_MIN_MS 20U
#define XIAOTAI_AUDIO_PAUSE_MAX_MS 800U
#define XIAOTAI_AUDIO_PAUSE_RESET_MS 2000U
/* The BK Opus decoder conceals one or two missing 20 ms frames. */
#define XIAOTAI_AUDIO_PAUSE_DECODER_SPAN_MS 40U

typedef struct {
    uint32_t next_ms;
    uint8_t stream;
    bool valid;
} xiaotai_audio_pause_t;

typedef struct {
    uint32_t total_ms;
    uint32_t count;
    uint32_t max_ms;
} xiaotai_audio_pause_stats_t;

static inline void xiaotai_audio_pause_reset(xiaotai_audio_pause_t *state)
{
    state->valid = false;
}

static inline void xiaotai_audio_pause_stats_add(
    xiaotai_audio_pause_stats_t *stats, uint32_t pause_ms)
{
    if (pause_ms == 0U) return;
    stats->total_ms += pause_ms;
    ++stats->count;
    if (pause_ms > stats->max_ms) stats->max_ms = pause_ms;
}

/* Returns silence (ms) to write before this packet. `samples` is the packet's
 * 16 kHz sample count. concealed_ms is 0 when the decoder conceals nothing. */
static inline uint32_t xiaotai_audio_pause_plan(xiaotai_audio_pause_t *state,
    uint32_t media_ms, uint8_t stream, uint32_t samples, uint32_t concealed_ms)
{
    uint32_t duration_ms = samples / 16U;
    uint32_t pause = 0U;
    if (duration_ms == 0U) return 0U;
    if (!state->valid || stream != state->stream) {
        state->valid = true;
        state->stream = stream;
        state->next_ms = media_ms + duration_ms;
        return 0U;
    }
    if (xiaotai_audio_recovery_before(media_ms, state->next_ms)) {
        if ((uint32_t)(state->next_ms - media_ms) > XIAOTAI_AUDIO_PAUSE_RESET_MS)
            state->next_ms = media_ms + duration_ms;
        return 0U;
    }
    uint32_t gap = media_ms - state->next_ms;
    uint32_t min_ms = XIAOTAI_AUDIO_PAUSE_MIN_MS;
    if (concealed_ms >= min_ms) min_ms = concealed_ms + XIAOTAI_AUDIO_PAUSE_MIN_MS;
    if (gap >= min_ms)
        pause = gap < XIAOTAI_AUDIO_PAUSE_MAX_MS ? gap : XIAOTAI_AUDIO_PAUSE_MAX_MS;
    state->next_ms = media_ms + duration_ms;
    return pause;
}

#endif
