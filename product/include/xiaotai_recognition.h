#ifndef XIAOTAI_RECOGNITION_H
#define XIAOTAI_RECOGNITION_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#define XIAOTAI_RECOGNITION_PRE_SAMPLES 80000U
#define XIAOTAI_RECOGNITION_POST_SAMPLES 160000U
#define XIAOTAI_RECOGNITION_SAMPLES 240000U
/* Single writer, caller provides serialized access and a 15-second PCM16 buffer. */
typedef struct {
    int16_t *pcm;
    size_t pre_count, head, count, post_count;
    bool recording, ready;
    int64_t last_ms, next_ms;
} xiaotai_recognition_t;
void xiaotai_recognition_init(xiaotai_recognition_t *r, int16_t *pcm);
void xiaotai_recognition_feed(xiaotai_recognition_t *r, const int16_t *pcm,
    size_t count, bool sound, bool allowed, int64_t now_ms);
void xiaotai_recognition_release(xiaotai_recognition_t *r, int64_t accepted_ms);
/* Validate complete PCM16 WAV and convert to mono 8 kHz for the prompt path. */
bool xiaotai_recognition_wav(const uint8_t *wav, size_t bytes,
    int16_t *output, size_t capacity, size_t *samples);

#endif
