#ifndef STARTER_AUDIO_CAPTURE_H
#define STARTER_AUDIO_CAPTURE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

/* Opt-in diagnostic storage. Caller serializes arm/record/stop/export.
 * No allocation, logging or file I/O in the media worker. Records contain
 * private audio; export only on an explicit development-console request.
 * Wire format: twelve little-endian uint32 fields, then Opus and PCM bytes. */
enum {
    STARTER_AUDIO_CAPTURE_STOPPED,
    STARTER_AUDIO_CAPTURE_RECORDING,
    STARTER_AUDIO_CAPTURE_FULL,
    STARTER_AUDIO_CAPTURE_GENERATION,
};
typedef struct {
    uint32_t generation, timestamp_ms, sample_rate, channels, bits, consumed;
    uint32_t decode_ms, write_ms, played;
} starter_audio_capture_meta_t;
typedef struct {
    uint8_t *storage;
    size_t capacity, used;
    uint32_t count, generation, reason;
    bool armed;
} starter_audio_capture_t;

static inline void starter_audio_capture_arm(starter_audio_capture_t *c,
                                             uint8_t *storage, size_t capacity)
{
    *c = (starter_audio_capture_t){.storage=storage, .capacity=capacity,
        .armed=storage != NULL && capacity >= 48,
        .reason=STARTER_AUDIO_CAPTURE_RECORDING};
}
static inline void starter_audio_capture_stop(starter_audio_capture_t *c)
{
    if (c->armed) c->reason = STARTER_AUDIO_CAPTURE_STOPPED;
    c->armed = false;
}
static inline bool starter_audio_capture_record(starter_audio_capture_t *c,
    const starter_audio_capture_meta_t *m, const void *opus, size_t opus_bytes,
    const void *pcm, size_t pcm_bytes)
{
    if (!c->armed || !m || !opus || !pcm || !opus_bytes || !pcm_bytes) return false;
    if (c->count && c->generation != m->generation) {
        c->armed=false;c->reason=STARTER_AUDIO_CAPTURE_GENERATION;return false;
    }
    size_t left=c->capacity-c->used;
    if (left<48 || opus_bytes>UINT32_MAX || pcm_bytes>UINT32_MAX ||
        opus_bytes>left-48 || pcm_bytes>left-48-opus_bytes) {
        c->armed=false;c->reason=STARTER_AUDIO_CAPTURE_FULL;return false;
    }
    uint32_t fields[]={0x31445541, m->generation, m->timestamp_ms,
        m->sample_rate,m->channels,m->bits,m->consumed,(uint32_t)opus_bytes,
        (uint32_t)pcm_bytes,m->decode_ms,m->write_ms,m->played};
    uint8_t *p=c->storage+c->used;
    for (unsigned i=0;i<12;i++) for (unsigned b=0;b<4;b++)
        *p++=(uint8_t)(fields[i]>>(b*8));
    memcpy(p,opus,opus_bytes);memcpy(p+opus_bytes,pcm,pcm_bytes);
    c->used+=48+opus_bytes+pcm_bytes;c->count++;c->generation=m->generation;
    return true;
}
#endif
