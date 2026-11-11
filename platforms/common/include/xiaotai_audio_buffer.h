#ifndef XIAOTAI_AUDIO_BUFFER_H
#define XIAOTAI_AUDIO_BUFFER_H

#include <stdbool.h>
#include <stdint.h>

/** Small AI playout gate. Owned by the playback worker (or its queue lock).
 * Durations are media time, not packet counts. All time differences must be
 * shorter than INT32_MAX ms. End is a release hint, never packet admission. */
typedef struct {
    uint32_t target_ms;
    uint32_t stable_ms;
    uint32_t starvation_ms;
    uint32_t wait_started_ms;
    uint32_t wait_target_ms;
    uint32_t underruns;
    uint32_t timeout_releases;
    bool waiting;
    bool playing;
    bool recovering;
} xiaotai_audio_buffer_t;

#ifndef AI_BUFFER_DEFAULT_MS
#define AI_BUFFER_DEFAULT_MS 100U
#endif
#ifndef AI_BUFFER_MIN_MS
#define AI_BUFFER_MIN_MS 60U
#endif
#ifndef AI_BUFFER_MAX_MS
#define AI_BUFFER_MAX_MS 300U
#endif
#ifndef AI_BUFFER_RECOVERY_MS
#define AI_BUFFER_RECOVERY_MS 140U
#endif
#ifndef AI_BUFFER_WAIT_LIMIT_MS
#define AI_BUFFER_WAIT_LIMIT_MS 400U
#endif
/* Do not count a normal sink/receive scheduling handoff as starvation. */
#ifndef AI_BUFFER_STARVATION_GRACE_MS
#define AI_BUFFER_STARVATION_GRACE_MS 20U
#endif
/* With no round boundary, long gaps are ambiguous (TTS/turn/network).
 * Rebuffer them, but do not train the target on silence. */
#ifndef AI_BUFFER_TRAIN_GAP_MS
#define AI_BUFFER_TRAIN_GAP_MS 1000U
#endif

_Static_assert(AI_BUFFER_MIN_MS > 0U &&
               AI_BUFFER_MIN_MS <= AI_BUFFER_DEFAULT_MS &&
               AI_BUFFER_DEFAULT_MS <= AI_BUFFER_MAX_MS &&
               AI_BUFFER_RECOVERY_MS <= AI_BUFFER_MAX_MS &&
               AI_BUFFER_WAIT_LIMIT_MS >= AI_BUFFER_MAX_MS,
               "AI watermarks must fit the bounded wait");

static inline void xiaotai_audio_buffer_reset(xiaotai_audio_buffer_t *buffer)
{
    *buffer = (xiaotai_audio_buffer_t){.target_ms = AI_BUFFER_DEFAULT_MS};
}

static inline void xiaotai_audio_buffer_begin_round(xiaotai_audio_buffer_t *buffer)
{
    buffer->playing = false;
    buffer->recovering = false;
    buffer->waiting = false;
}

static inline bool xiaotai_audio_buffer_ready(xiaotai_audio_buffer_t *buffer,
    uint32_t now_ms, uint32_t queued_ms, uint32_t oldest_rx_ms,
    uint32_t playback_tail_ms, bool ended)
{
    if (queued_ms == 0U) {
        if (ended) {
            xiaotai_audio_buffer_begin_round(buffer);
        } else if (buffer->playing &&
                   (int32_t)(now_ms - playback_tail_ms) >=
                       (int32_t)AI_BUFFER_STARVATION_GRACE_MS) {
            buffer->playing = false;
            buffer->recovering = true;
            buffer->starvation_ms = playback_tail_ms;
        }
        return false;
    }
    /* A worker may block waiting for its next packet rather than poll an empty
     * queue. Arrival after the estimated DAC tail is also supply starvation;
     * old queued packets decoded late must not be classified this way. */
    if (!ended && buffer->playing &&
        (int32_t)(oldest_rx_ms - playback_tail_ms) >=
            (int32_t)AI_BUFFER_STARVATION_GRACE_MS) {
        buffer->playing = false;
        buffer->recovering = true;
        buffer->starvation_ms = playback_tail_ms;
    }
    if (ended || buffer->playing) {
        buffer->playing = true;
        buffer->waiting = false;
        buffer->recovering = false;
        return true;
    }
    if (!buffer->waiting) {
        buffer->waiting = true;
        buffer->wait_started_ms = oldest_rx_ms;
        buffer->wait_target_ms = buffer->target_ms;
        if (buffer->recovering) {
            /* Classify once, when data returns. Empty polls cannot inflate delay.
             * Use arrival time, not a delayed worker's observation time. */
            if ((int32_t)(oldest_rx_ms - buffer->starvation_ms) >=
                    (int32_t)AI_BUFFER_STARVATION_GRACE_MS &&
                (uint32_t)(oldest_rx_ms - buffer->starvation_ms) <
                    AI_BUFFER_TRAIN_GAP_MS) {
                buffer->target_ms += 40U;
                if (buffer->target_ms > AI_BUFFER_MAX_MS)
                    buffer->target_ms = AI_BUFFER_MAX_MS;
                buffer->stable_ms = 0U;
                ++buffer->underruns;
            }
            buffer->recovering = false;
            buffer->wait_target_ms = buffer->target_ms;
            if (buffer->wait_target_ms < AI_BUFFER_RECOVERY_MS)
                buffer->wait_target_ms = AI_BUFFER_RECOVERY_MS;
        }
    }
    uint32_t target_ms = buffer->wait_target_ms;
    if (queued_ms < target_ms &&
        (uint32_t)(now_ms - buffer->wait_started_ms) < AI_BUFFER_WAIT_LIMIT_MS)
        return false;
    if (queued_ms < target_ms) ++buffer->timeout_releases;
    buffer->playing = true;
    buffer->waiting = false;
    return true;
}

static inline void xiaotai_audio_buffer_written(xiaotai_audio_buffer_t *buffer,
    uint32_t duration_ms)
{
    buffer->stable_ms += duration_ms;
    if (buffer->stable_ms >= 10000U) {
        buffer->stable_ms = 0U;
        if (buffer->target_ms > AI_BUFFER_MIN_MS) {
            buffer->target_ms -= 20U;
            if (buffer->target_ms < AI_BUFFER_MIN_MS)
                buffer->target_ms = AI_BUFFER_MIN_MS;
        }
    }
}

#endif
