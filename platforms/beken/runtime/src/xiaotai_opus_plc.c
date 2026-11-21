#include "xiaotai_opus_plc.h"
#include <modules/opus.h>
#include <stddef.h>

enum { FRAME_SAMPLES = 320 };

int xiaotai_opus_configure_loss(OpusEncoder *encoder)
{
    if (encoder == NULL) return OPUS_BAD_ARG;
    int rc = opus_encoder_ctl(encoder, OPUS_SET_BITRATE(20000));
    if (rc != OPUS_OK) return rc;
    rc = opus_encoder_ctl(encoder, OPUS_SET_PACKET_LOSS_PERC(30));
    if (rc != OPUS_OK) return rc;
    return opus_encoder_ctl(encoder, OPUS_SET_INBAND_FEC(1));
}

int xiaotai_opus_plc_decode(xiaotai_opus_plc_t *state, OpusDecoder *decoder,
    const xiaotai_opus_stamp_t *stamp, const uint8_t *packet, int bytes,
    int16_t *pcm, int pcm_capacity)
{
    if (state == NULL || decoder == NULL || stamp == NULL || packet == NULL ||
        pcm == NULL || bytes <= 0 || pcm_capacity < 0) return OPUS_BAD_ARG;
    int packet_samples = opus_packet_get_nb_samples(packet, bytes, 16000);
    if (packet_samples <= 0) return packet_samples < 0 ? packet_samples : OPUS_INVALID_PACKET;
    if (packet_samples != stamp->samples) return OPUS_BAD_ARG;
    bool fixed_voice_frame = bytes > 2 && packet_samples == FRAME_SAMPLES;
    xiaotai_audio_recovery_plan_t plan = xiaotai_audio_recovery_plan(
        &state->timeline, stamp, true, fixed_voice_frame);
    if (plan.discard) {
        state->output_concealed_samples = 0;
        state->output_packet_decoded = false;
        return 0;
    }
    /* Preflight before resetting or decoding: callers can retry a larger
     * buffer without consuming either the packet or its concealment. */
    if (pcm_capacity < packet_samples + plan.missing_frames * FRAME_SAMPLES)
        return OPUS_BUFFER_TOO_SMALL;
    state->output_concealed_samples = 0;
    state->output_packet_decoded = false;
    if (plan.reset) {
        int rc = opus_decoder_ctl(decoder, OPUS_RESET_STATE);
        if (rc != OPUS_OK) {
            state->timeline.desync = true;
            return rc;
        }
        xiaotai_audio_recovery_begin(&state->timeline, stamp);
    }
    int prefix = 0;
    for (unsigned i = 0; i < plan.missing_frames; ++i) {
        /* Only the immediately preceding frame can use this packet's FEC.
         * With no usable redundancy libopus itself falls back to PLC. */
        bool fec = i + 1 == plan.missing_frames;
        int rc = opus_decode(decoder, fec ? packet : NULL, fec ? bytes : 0,
            pcm + prefix, FRAME_SAMPLES, fec ? 1 : 0);
        if (rc != FRAME_SAMPLES) {
            state->timeline.desync = true;
            return prefix ? prefix : (rc < 0 ? rc : OPUS_INTERNAL_ERROR);
        }
        prefix += FRAME_SAMPLES;
        state->output_concealed_samples = (uint16_t)prefix;
        xiaotai_audio_recovery_commit_missing(&state->timeline);
    }
    int decoded = opus_decode(decoder, packet, bytes, pcm + prefix,
        pcm_capacity - prefix, 0);
    if (decoded != packet_samples) {
        state->timeline.desync = true;
        return prefix ? prefix : (decoded < 0 ? decoded : OPUS_INTERNAL_ERROR);
    }
    xiaotai_audio_recovery_commit_packet(&state->timeline, stamp, plan, fixed_voice_frame);
    state->output_packet_decoded = true;
    return prefix + decoded;
}
