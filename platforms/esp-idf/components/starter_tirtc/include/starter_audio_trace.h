#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
/* Opt-in, bounded packet metadata only. Never logs or allocates in callbacks.
 * Identity is (generation, timestamp, format, length, hash); duplicates remain
 * separate observations. Hash equality alone is not proof of packet identity. */
enum { AUDIO_TRACE_CALLBACK=1, AUDIO_TRACE_ENQUEUE, AUDIO_TRACE_DEQUEUE,
       AUDIO_TRACE_WRITTEN };
typedef struct {
    uint32_t stage, generation, timestamp_ms, length, media, flags, stream_id;
    uint32_t hash, at_ms, detail;
} starter_audio_trace_row_t;
bool starter_audio_trace_arm(void);
void starter_audio_trace_stop(void);
void starter_audio_trace_clear(void);
void starter_audio_trace_status(size_t *count, uint32_t *lost, bool *armed);
bool starter_audio_trace_read(size_t index, starter_audio_trace_row_t *row);
void starter_audio_trace_record(unsigned stage, uint32_t generation,
    uint32_t timestamp_ms, uint32_t length, unsigned media, unsigned flags,
    unsigned stream_id, const void *data, uint32_t at_ms, uint32_t detail);
