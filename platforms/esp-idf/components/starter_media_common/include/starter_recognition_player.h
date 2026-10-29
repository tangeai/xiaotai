#ifndef STARTER_RECOGNITION_PLAYER_H
#define STARTER_RECOGNITION_PLAYER_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdatomic.h>
/* Recognition owns this state; no call/prompt queues or cancellation epochs. */
typedef struct { atomic_uint epoch; atomic_bool busy; } starter_recognition_player_t;
typedef struct {
    void *context;
    int (*begin)(void *, unsigned);
    int (*write)(void *, const int16_t *, size_t, unsigned);
    int (*end)(void *);
} starter_recognition_output_t;
unsigned starter_recognition_player_epoch(starter_recognition_player_t *);
bool starter_recognition_player_busy(starter_recognition_player_t *);
void starter_recognition_player_cancel(starter_recognition_player_t *);
/* 16 kHz mono PCM16 -> stereo without gain or resampling. Output ports must
 * recheck epoch/foreground ownership after acquiring their per-block lock. */
int starter_recognition_player_play(starter_recognition_player_t *,
    const starter_recognition_output_t *, const int16_t *, size_t, unsigned);
#endif
