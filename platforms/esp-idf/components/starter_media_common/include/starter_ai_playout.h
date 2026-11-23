#ifndef STARTER_AI_PLAYOUT_H
#define STARTER_AI_PLAYOUT_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* Fixed 16 kHz mono AI downlink. Accounting includes the sink-held packet;
 * each accepted packet must be retired once (including reject/flush paths).
 * Generation checks prevent an old sink from decrementing a new session. */
typedef struct {
    uint32_t queued_samples;
    uint32_t target_ms;
    uint32_t underruns;
    uint32_t waits;
    uint32_t timeout_releases;
} starter_ai_playout_status_t;

void starter_ai_playout_reset(uint32_t generation);
/* TOC duration only; the decoder still validates the full compressed payload. */
uint16_t starter_ai_playout_admit(uint32_t generation,
                                 const uint8_t *packet, size_t size);
void starter_ai_playout_retire(uint32_t generation, uint16_t samples);
/* Sender pauses. The sender omits silent frames, so back-to-back playback
 * would remove the pauses between words. Given the next packet the sink is
 * about to play, return the silence (ms) the sender's media clock leaves
 * before it: whole frames only, capped at STARTER_AI_PAUSE_MAX_MS so a stall
 * or clock jump cannot add unbounded delay. Zero for the first packet of a
 * turn, a stream change, late/duplicate packets and stale generations.
 * Only the sink worker calls this, once per packet it will play. */
#define STARTER_AI_PAUSE_MIN_MS 20U
#define STARTER_AI_PAUSE_MAX_MS 800U
#define STARTER_AI_PAUSE_RESET_MS 2000U
uint16_t starter_ai_playout_pause_ms(uint32_t generation, uint32_t media_ms,
                                     uint8_t stream, uint16_t samples);
bool starter_ai_playout_ready(uint32_t generation, uint32_t now_ms,
                             uint32_t first_rx_ms, uint32_t dma_tail_ms);
void starter_ai_playout_written(uint32_t generation, size_t samples, uint32_t now_ms);
/* Called by runtime after validating its session generation; safe if stale. */
void starter_media_ai_playback_boundary(uint32_t generation, bool ended);
starter_ai_playout_status_t starter_ai_playout_status(void);

#endif
