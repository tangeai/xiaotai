#ifndef STARTER_PREROLL_H
#define STARTER_PREROLL_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* PCM queue at 8 or 16 kHz. Caller serializes access; no SDK calls or allocation. */
#define PREROLL_RATE 8000U
#define PREROLL_HISTORY_MS 1500U
#define PREROLL_CAPACITY_SAMPLES (PREROLL_RATE * 20U)
#define PREROLL_TIMEOUT_MS 19000
#define PREROLL_ACK_QUEUE_TIMEOUT_MS 2000
#define PREROLL_ACK_PLAY_TIMEOUT_MS 5000
#define PREROLL_ACK_TAIL_MS 160
typedef struct {
    int16_t *pcm;
    size_t capacity, head, count;
    uint32_t sample_rate_hz;
    uint32_t next_token, token, generation;
    int64_t end_ms, expires_ms, timestamp_ms;
    bool failed;
    bool ack_pending, ack_playing;
    int64_t ack_deadline_ms, listen_after_ms;
} starter_preroll_t;

void starter_preroll_init(starter_preroll_t *q, int16_t *pcm, size_t capacity);
void starter_preroll_init_rate(starter_preroll_t *q, int16_t *pcm,
                              size_t capacity, uint32_t sample_rate_hz);
void starter_preroll_clear(starter_preroll_t *q);
void starter_preroll_append(starter_preroll_t *q, const int16_t *pcm,
                           size_t samples, int64_t end_ms);
uint32_t starter_preroll_prepare(starter_preroll_t *q, int64_t wake_ms, int64_t now_ms);
/* Acoustic acknowledgement gate; caller keeps capture/AEC running but drops PCM.
 * A missing/stuck worker expires instead of opening the microphone by timeout. */
void starter_preroll_hold_ack(starter_preroll_t *q, uint32_t token, int64_t now_ms);
bool starter_preroll_begin_ack(starter_preroll_t *q, uint32_t token, int64_t now_ms);
void starter_preroll_finish_ack(starter_preroll_t *q, uint32_t token, int64_t now_ms);
bool starter_preroll_input_ready(const starter_preroll_t *q);
bool starter_preroll_valid(const starter_preroll_t *q, uint32_t token, int64_t now_ms);
bool starter_preroll_bind(starter_preroll_t *q, uint32_t token,
                         uint32_t generation, int64_t now_ms);
bool starter_preroll_peek(starter_preroll_t *q, uint32_t generation,
                         int16_t *pcm, size_t samples, uint32_t *timestamp_ms);
void starter_preroll_consume(starter_preroll_t *q, size_t samples);
/* Transfer the last partial packet and release replay ownership atomically. */
bool starter_preroll_finish_replay(starter_preroll_t *q, uint32_t generation,
                                  int16_t *tail, size_t packet_samples,
                                  size_t *tail_samples, uint32_t *timestamp_ms);
/* Only setup is all-or-nothing. Active media discontinuities must recover. */
bool starter_preroll_failure_requires_abort(const starter_preroll_t *q,
                                            uint32_t token, int64_t now_ms);
#endif
