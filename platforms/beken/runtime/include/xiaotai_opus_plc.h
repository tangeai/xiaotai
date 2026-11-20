#ifndef XIAOTAI_OPUS_PLC_H
#define XIAOTAI_OPUS_PLC_H

#include <stdint.h>

struct OpusDecoder;
typedef struct {
    uint32_t media_ms, rx_ms, epoch;
    uint16_t samples;
    uint8_t stream;
} xiaotai_opus_stamp_t;

/* Owned exclusively by the playback worker, zeroed on session start. */
typedef struct {
    xiaotai_opus_stamp_t last;
    uint32_t repair_start_ms, repair_rx_ms;
    int16_t history[320];
    uint8_t steady_intervals, repair_frames;
} xiaotai_opus_plc_t;

/* Returns samples including any PLC prefix, zero for a late frame already
 * concealed, or the libopus error. pcm_capacity is in mono samples. */
int xiaotai_opus_plc_decode(xiaotai_opus_plc_t *state, struct OpusDecoder *decoder,
    const xiaotai_opus_stamp_t *stamp, const uint8_t *packet, int bytes,
    int16_t *pcm, int pcm_capacity);

#endif
