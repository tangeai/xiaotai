#include "xiaotai_opus_plc.h"
#include <modules/opus.h>
#include <stdbool.h>
#include <string.h>

enum { FRAME_MS = 20, FRAME_SAMPLES = 320, MAX_GAP_FRAMES = 2,
       REQUIRED_INTERVALS = 2, MAX_ARRIVAL_GAP_MS = 120, BLEND_SAMPLES = 32 };

/* OPUS_GET_PITCH reports a period at 48 kHz. Reuse the last decoded
 * waveform at 16 kHz; unvoiced packets can lack a usable pitch estimate. */
static void extend_history(const int16_t *history, opus_int32 pitch_48k,
                           int16_t *pcm, int missing_samples)
{
    int period = pitch_48k / 3;
    if (period < 24 || period > FRAME_SAMPLES) period = FRAME_SAMPLES;
    int pos = FRAME_SAMPLES - period;
    for (int i = 0; i < missing_samples; ++i) {
        pcm[i] = history[pos++];
        if (pos == FRAME_SAMPLES) pos -= period;
    }
    /* The real packet is already decoded at pcm[missing_samples]. Smooth
     * only the two synthetic edges. All real samples remain unmodified. */
    for (int j = 0; j < BLEND_SAMPLES; ++j) {
        int remaining = BLEND_SAMPLES - j;
        pcm[j] = (pcm[j] * j + history[FRAME_SAMPLES - 1] * remaining) / BLEND_SAMPLES;
        int end = missing_samples - BLEND_SAMPLES + j;
        pcm[end] = (pcm[end] * remaining + pcm[missing_samples] * j) / BLEND_SAMPLES;
    }
}

int xiaotai_opus_plc_decode(xiaotai_opus_plc_t *state, OpusDecoder *decoder,
    const xiaotai_opus_stamp_t *stamp, const uint8_t *packet, int bytes,
    int16_t *pcm, int pcm_capacity)
{
    const xiaotai_opus_stamp_t *last = &state->last;
    bool continuous = bytes > 2 && last->samples == FRAME_SAMPLES && stamp->samples == FRAME_SAMPLES &&
        last->epoch == stamp->epoch && last->stream == stamp->stream &&
        stamp->rx_ms - last->rx_ms <= MAX_ARRIVAL_GAP_MS;
    uint32_t delta = stamp->media_ms - last->media_ms;
    if (!continuous || stamp->rx_ms - state->repair_rx_ms > MAX_ARRIVAL_GAP_MS)
        state->repair_frames = 0;
    uint32_t late = stamp->media_ms - state->repair_start_ms;
    if (late < state->repair_frames * FRAME_MS && late % FRAME_MS == 0)
        return 0;

    int prefix = 0;
    /* Require a previously observed regular media clock. Packet arrival
     * spacing is only a veto for long pauses, never evidence of loss. */
    if (continuous && state->steady_intervals == REQUIRED_INTERVALS &&
        (delta == 2 * FRAME_MS || delta == (MAX_GAP_FRAMES + 1) * FRAME_MS)) {
        int frames = (int)(delta / FRAME_MS) - 1;
        int missing = frames * FRAME_SAMPLES;
        if (pcm_capacity >= missing + FRAME_SAMPLES) prefix = missing;
    }
    opus_int32 pitch = 0;
    if (prefix && opus_decoder_ctl(decoder, OPUS_GET_PITCH(&pitch)) != OPUS_OK)
        pitch = 0;
    int decoded = opus_decode(decoder, packet, bytes, pcm + prefix, pcm_capacity - prefix, 0);
    if (decoded < 0) {
        *state = (xiaotai_opus_plc_t){0};
        return decoded;
    }
    if (prefix) {
        extend_history(state->history, pitch, pcm, prefix);
        state->repair_start_ms = last->media_ms + FRAME_MS;
        state->repair_rx_ms = stamp->rx_ms;
        state->repair_frames = (uint8_t)(prefix / FRAME_SAMPLES);
    }
    if (bytes > 2 && decoded == FRAME_SAMPLES)
        memcpy(state->history, pcm + prefix, sizeof(state->history));
    if (continuous && delta == FRAME_MS) {
        if (state->steady_intervals < REQUIRED_INTERVALS) ++state->steady_intervals;
    } else {
        state->steady_intervals = 0;
    }
    state->last = *stamp;
    /* Opus permits omitting very short DTX packets. Silence must break the
     * cadence, even when the sender continues to publish timestamps. */
    if (bytes <= 2 || decoded != FRAME_SAMPLES) state->last.samples = 0;
    return prefix + decoded;
}
