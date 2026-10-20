#pragma once
#include <stddef.h>
#include <stdint.h>
/* ES7210 reg12=0x02 TDM order: MIC1, MIC3, MIC2, MIC4.
 * MIC3 is the differential electrical speaker reference (schematic page 3). */
static inline void cores3_audio_unpack(int16_t *out, const int16_t *wire, size_t frames)
{
    for (size_t i = 0; i < frames; ++i) {
        out[2*i] = wire[4*i];
        out[2*i+1] = wire[4*i+1];
    }
}
