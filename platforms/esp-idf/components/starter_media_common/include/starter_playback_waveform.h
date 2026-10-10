#ifndef STARTER_PLAYBACK_WAVEFORM_H
#define STARTER_PLAYBACK_WAVEFORM_H

#include <stdbool.h>
#include <stdint.h>

#define STARTER_PLAYBACK_WAVEFORM_POINTS 32U
typedef struct {
    bool active;
    int8_t points[STARTER_PLAYBACK_WAVEFORM_POINTS]; /* signed pixel offset, -16..16 */
} starter_playback_waveform_t;

#endif
