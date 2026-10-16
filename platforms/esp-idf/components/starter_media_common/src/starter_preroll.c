#include "starter_preroll.h"
#include <string.h>

void starter_preroll_init(starter_preroll_t *q, int16_t *pcm, size_t capacity)
{
    *q = (starter_preroll_t){.pcm = pcm, .capacity = capacity};
}

void starter_preroll_clear(starter_preroll_t *q)
{
    q->head = q->count = 0;
    q->token = q->generation = 0;
    q->end_ms = q->expires_ms = q->timestamp_ms = 0;
    q->failed = false;
    q->ack_pending = q->ack_playing = false;
    q->ack_deadline_ms = q->listen_after_ms = 0;
}

void starter_preroll_append(starter_preroll_t *q, const int16_t *pcm,
                           size_t samples, int64_t end_ms)
{
    if (q->failed || q->capacity == 0 || samples == 0) return;
    /* Drop complete frames overlapping the local prompt and its acoustic tail.
     * First admitted frame establishes a fresh timestamp/continuity boundary. */
    if (q->ack_pending) return;
    if (q->listen_after_ms != 0) {
        if (end_ms - (int64_t)samples / 8 < q->listen_after_ms) return;
        q->listen_after_ms = 0;
        q->timestamp_ms = end_ms - (int64_t)samples / 8;
        q->end_ms = 0;
    }
    /* An input discontinuity invalidates a pending utterance, never splices it. */
    int64_t duration_ms = (int64_t)samples / 8;
    if (q->end_ms != 0 &&
        (end_ms <= q->end_ms || end_ms - q->end_ms > duration_ms + 100)) {
        if (q->token != 0) { q->failed = true; return; }
        starter_preroll_clear(q);
    }
    if (q->token != 0 &&
        ((q->generation == 0 && end_ms > q->expires_ms) ||
         samples > q->capacity - q->count)) {
        q->failed = true;
        return;
    }
    size_t limit = q->token ? q->capacity :
                   (q->capacity < PREROLL_HISTORY_SAMPLES ? q->capacity : PREROLL_HISTORY_SAMPLES);
    for (size_t i = 0; i < samples; ++i) {
        if (q->count == limit) {
            q->head = (q->head + 1U) % q->capacity;
            --q->count;
        }
        q->pcm[(q->head + q->count) % q->capacity] = pcm[i];
        ++q->count;
    }
    q->end_ms = end_ms;
}

uint32_t starter_preroll_prepare(starter_preroll_t *q, int64_t wake_ms, int64_t now_ms)
{
    if (q->token != 0 || q->failed || q->count == 0 || wake_ms <= 0 ||
        now_ms < wake_ms || now_ms - wake_ms > 1000 ||
        now_ms - q->end_ms > 150 || wake_ms > q->end_ms) return 0;
    /* The detector reports the end of its acoustic input, not the time the
     * inference callback ran. Keep subsequent speech through inference and
     * connection setup, but never deliberately replay the detector's window.
     * Empty post-wake audio is valid: future capture belongs to this token. */
    int64_t keep_ms = q->end_ms - wake_ms;
    size_t keep = (size_t)keep_ms * 8U;
    if (keep < q->count) {
        size_t drop = q->count - keep;
        q->head = (q->head + drop) % q->capacity;
        q->count = keep;
    }
    if (++q->next_token == 0) ++q->next_token;
    q->token = q->next_token;
    q->timestamp_ms = q->end_ms - (int64_t)q->count / 8;
    q->expires_ms = now_ms + PREROLL_TIMEOUT_MS;
    return q->token;
}

void starter_preroll_hold_ack(starter_preroll_t *q, uint32_t token, int64_t now_ms)
{
    if (!token || q->token != token || q->generation) return;
    q->head = q->count = 0;
    q->ack_pending = true;
    q->ack_playing = false;
    q->ack_deadline_ms = now_ms + PREROLL_ACK_QUEUE_TIMEOUT_MS;
}

bool starter_preroll_begin_ack(starter_preroll_t *q, uint32_t token, int64_t now_ms)
{
    if (!starter_preroll_valid(q, token, now_ms) || !q->ack_pending ||
        q->ack_playing || q->generation) return false;
    q->ack_playing = true;
    q->ack_deadline_ms = now_ms + PREROLL_ACK_PLAY_TIMEOUT_MS;
    return true;
}

void starter_preroll_finish_ack(starter_preroll_t *q, uint32_t token, int64_t now_ms)
{
    if (!starter_preroll_valid(q, token, now_ms) || !q->ack_pending || q->generation) return;
    q->ack_pending = q->ack_playing = false;
    q->ack_deadline_ms = 0;
    q->listen_after_ms = now_ms + PREROLL_ACK_TAIL_MS;
}

bool starter_preroll_input_ready(const starter_preroll_t *q)
{
    return !q->ack_pending && q->listen_after_ms == 0;
}

bool starter_preroll_valid(const starter_preroll_t *q, uint32_t token, int64_t now_ms)
{
    return token != 0 && q->token == token && !q->failed &&
           (q->generation != 0 || now_ms <= q->expires_ms) &&
           (!q->ack_pending || now_ms <= q->ack_deadline_ms);
}

bool starter_preroll_bind(starter_preroll_t *q, uint32_t token,
                         uint32_t generation, int64_t now_ms)
{
    if (generation == 0 || !starter_preroll_valid(q, token, now_ms) ||
        q->generation != 0 || !starter_preroll_input_ready(q)) return false;
    q->generation = generation;
    return true;
}

bool starter_preroll_peek(starter_preroll_t *q, uint32_t generation,
                         int16_t *pcm, size_t samples, uint32_t *timestamp_ms)
{
    if (generation == 0 || q->generation != generation || q->failed ||
        samples > q->count) return false;
    for (size_t i = 0; i < samples; ++i) pcm[i] = q->pcm[(q->head + i) % q->capacity];
    *timestamp_ms = (uint32_t)q->timestamp_ms;
    return true;
}

void starter_preroll_consume(starter_preroll_t *q, size_t samples)
{
    if (samples > q->count) return;
    q->head = (q->head + samples) % q->capacity;
    q->count -= samples;
    q->timestamp_ms += (int64_t)samples / 8;
}

bool starter_preroll_finish_replay(starter_preroll_t *q, uint32_t generation,
                                  int16_t *tail, size_t packet_samples,
                                  size_t *tail_samples, uint32_t *timestamp_ms)
{
    if (!tail || !tail_samples || !timestamp_ms || !q->token || !generation ||
        q->generation != generation || q->failed || q->count >= packet_samples) return false;
    *tail_samples = q->count;
    *timestamp_ms = (uint32_t)q->timestamp_ms;
    for (size_t i = 0; i < q->count; ++i) tail[i] = q->pcm[(q->head + i) % q->capacity];
    starter_preroll_clear(q);
    return true;
}

bool starter_preroll_failure_requires_abort(const starter_preroll_t *q,
                                            uint32_t token, int64_t now_ms)
{
    return token != 0 && q->generation == 0 && !starter_preroll_valid(q, token, now_ms);
}
