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
    uint32_t columns;
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
    scope->columns = 0U;
    scope->at_ms = now_ms;
    scope->generation = generation;
    scope->valid = true;
    atomic_flag_clear_explicit(&scope->lock, memory_order_release);
}

/* Same seven short-time amplitude slices as the BK compact mouth. This is
 * a level display, not an FFT. Skip alternate samples to bound audio work. */
static inline void starter_playback_scope_publish_dots(starter_playback_scope_t *scope,
    const int16_t *pcm, size_t count, uint32_t now_ms, uint32_t generation)
{
    if (atomic_load_explicit(&scope->disabled, memory_order_relaxed) ||
        pcm == NULL || count == 0U) return;
    static const uint16_t thresholds[7] = {160, 320, 640, 1200, 2400, 4800, 9600};
    uint32_t columns = 0U;
    for (size_t column = 0U; column < 7U; ++column) {
        size_t begin = column * count / 7U;
        size_t end = (column + 1U) * count / 7U;
        uint32_t peak = 0U;
        for (size_t i = begin; i < end; i += 2U) {
            int32_t sample = pcm[i];
            uint32_t magnitude = (uint32_t)(sample < 0 ? -sample : sample);
            if (magnitude > peak) peak = magnitude;
        }
        uint32_t height = 0U;
        while (height < 7U && peak >= thresholds[height]) ++height;
        columns |= height << (column * 4U);
    }
    if (atomic_flag_test_and_set_explicit(&scope->lock, memory_order_acquire)) return;
    scope->columns = columns;
    memset(scope->points, 0, sizeof(scope->points));
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
        out->columns = scope->columns;
    }
    atomic_flag_clear_explicit(&scope->lock, memory_order_release);
}
#endif
