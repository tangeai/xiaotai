#ifndef STARTER_PLAYBACK_SCOPE_H
#define STARTER_PLAYBACK_SCOPE_H

#include <stddef.h>
#include <stdatomic.h>
#include <string.h>
#include "starter_playback_waveform.h"

/* A bounded, best-effort visualization mailbox. Neither audio nor UI waits. */
typedef struct {
    atomic_flag lock;
    atomic_bool disabled;
    bool valid;
    uint32_t at_ms, generation;
    int8_t points[STARTER_PLAYBACK_WAVEFORM_POINTS];
} starter_playback_scope_t;

static inline void starter_playback_scope_publish(starter_playback_scope_t *scope,
    const int16_t *pcm, size_t count, uint32_t now_ms, uint32_t generation)
{
    if (atomic_load_explicit(&scope->disabled, memory_order_relaxed) ||
        pcm == NULL || count == 0U) return;
    int8_t points[STARTER_PLAYBACK_WAVEFORM_POINTS];
    for (size_t i = 0; i < STARTER_PLAYBACK_WAVEFORM_POINTS; ++i) {
        size_t start = i * count / STARTER_PLAYBACK_WAVEFORM_POINTS;
        size_t end = (i + 1U) * count / STARTER_PLAYBACK_WAVEFORM_POINTS;
        if (end <= start) end = start + 1U;
        int32_t peak = 0;
        for (size_t j = start; j < end; ++j) {
            int32_t sample = pcm[j];
            if ((sample < 0 ? -sample : sample) >
                (peak < 0 ? -peak : peak)) peak = sample;
        }
        int32_t offset = peak * 16 / 32767;
        if (offset > 16) offset = 16;
        if (offset < -16) offset = -16;
        points[i] = (int8_t)offset;
    }
    if (atomic_flag_test_and_set_explicit(&scope->lock, memory_order_acquire)) return;
    memcpy(scope->points, points, sizeof(points));
    scope->at_ms = now_ms;
    scope->generation = generation;
    scope->valid = true;
    atomic_flag_clear_explicit(&scope->lock, memory_order_release);
}

static inline void starter_playback_scope_read(starter_playback_scope_t *scope,
    uint32_t now_ms, uint32_t generation, bool enabled,
    starter_playback_waveform_t *out)
{
    memset(out, 0, sizeof(*out));
    if (!enabled || atomic_load_explicit(&scope->disabled, memory_order_relaxed) ||
        atomic_flag_test_and_set_explicit(&scope->lock, memory_order_acquire)) return;
    if (scope->valid && scope->generation == generation &&
        (uint32_t)(now_ms - scope->at_ms) <= 150U) {
        out->active = true;
        memcpy(out->points, scope->points, sizeof(out->points));
    }
    atomic_flag_clear_explicit(&scope->lock, memory_order_release);
}
#endif
