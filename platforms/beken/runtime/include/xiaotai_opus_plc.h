#ifndef XIAOTAI_OPUS_PLC_H
#define XIAOTAI_OPUS_PLC_H

#include <stdint.h>
#include "xiaotai_audio_recovery.h"

struct OpusDecoder;
struct OpusEncoder;

/* The loss expectation is an encoder setting; the receiver uses real gaps. */
int xiaotai_opus_configure_loss(struct OpusEncoder *encoder);
typedef xiaotai_audio_recovery_stamp_t xiaotai_opus_stamp_t;

/* Owned exclusively by the playback worker, zeroed on session start. */
typedef struct {
    xiaotai_audio_recovery_t timeline;
    uint16_t output_concealed_samples;
    bool output_packet_decoded;
} xiaotai_opus_plc_t;

/* Returns samples including any PLC prefix, zero for a late frame already
 * consumed, or the libopus error. A partially successful recovery returns
 * its valid prefix and marks the timeline desynchronized for the next packet.
 * pcm_capacity is in mono samples; insufficient capacity consumes nothing. */
int xiaotai_opus_plc_decode(xiaotai_opus_plc_t *state, struct OpusDecoder *decoder,
    const xiaotai_opus_stamp_t *stamp, const uint8_t *packet, int bytes,
    int16_t *pcm, int pcm_capacity);

#endif
