#include "xiaotai_opus_plc.h"
#include <modules/opus.h>
#include <stdbool.h>
#include <stddef.h>

enum { FRAME_MS = 20, FRAME_SAMPLES = 320, MAX_GAP_FRAMES = 2,
       REQUIRED_INTERVALS = 2, MAX_ARRIVAL_GAP_MS = 120 };

int xiaotai_opus_configure_loss(OpusEncoder *encoder)
{
    int rc = opus_encoder_ctl(encoder, OPUS_SET_BITRATE(20000));
    if (rc != OPUS_OK) return rc;
    rc = opus_encoder_ctl(encoder, OPUS_SET_PACKET_LOSS_PERC(30));
    if (rc != OPUS_OK) return rc;
    return opus_encoder_ctl(encoder, OPUS_SET_INBAND_FEC(1));
}

/* Only the immediately preceding frame can be recovered from the current
 * packet's FEC. libopus supplies PLC if that packet contains no redundancy. */
static int recover_gap(OpusDecoder *decoder, const uint8_t *packet, int bytes,
                       int16_t *pcm, int missing)
{
    if (missing == 2 * FRAME_SAMPLES) {
        int rc = opus_decode(decoder, NULL, 0, pcm, FRAME_SAMPLES, 0);
        if (rc != FRAME_SAMPLES) return 0;
    }
    int offset = missing - FRAME_SAMPLES;
    int rc = opus_decode(decoder, packet, bytes, pcm + offset, FRAME_SAMPLES, 1);
    return rc == FRAME_SAMPLES ? missing : offset;
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
        if (pcm_capacity >= missing + FRAME_SAMPLES)
            prefix = recover_gap(decoder, packet, bytes, pcm, missing);
    }
    int decoded = opus_decode(decoder, packet, bytes, pcm + prefix, pcm_capacity - prefix, 0);
    if (decoded < 0) {
        *state = (xiaotai_opus_plc_t){0};
        return decoded;
    }
    if (prefix) {
        state->repair_start_ms = last->media_ms + FRAME_MS;
        state->repair_rx_ms = stamp->rx_ms;
        state->repair_frames = (uint8_t)(prefix / FRAME_SAMPLES);
    }
    if (continuous && delta == FRAME_MS) {
        if (state->steady_intervals < REQUIRED_INTERVALS) ++state->steady_intervals;
    } else if (!prefix) {
        state->steady_intervals = 0;
    }
    state->last = *stamp;
    /* Opus permits omitting very short DTX packets. Silence must break the
     * cadence, even when the sender continues to publish timestamps. */
    if (bytes <= 2 || decoded != FRAME_SAMPLES) state->last.samples = 0;
    return prefix + decoded;
}
