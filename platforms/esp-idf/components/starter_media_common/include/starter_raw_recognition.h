#ifndef STARTER_RAW_RECOGNITION_H
#define STARTER_RAW_RECOGNITION_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* Capture-task-owned state shared by S3/P4. The hold matches the sound gate. */
typedef struct {
    uint32_t smoothed_rms;
    int64_t candidate_since_ms;
    bool candidate_sound;
    bool sound;
} starter_raw_recognition_t;

/* Copy one physical microphone slot into 16 kHz mono PCM and evaluate sound.
 * The output is unmodified ADC PCM; reference and AEC output never enter it.
 * Ineligible frames clear detector history so a session/prompt cannot trigger
 * recognition immediately after it ends. */
bool starter_raw_recognition_feed(starter_raw_recognition_t *state,
                                  const int16_t *capture, size_t frames,
                                  size_t channels, size_t microphone_slot,
                                  int16_t *mono, size_t capacity,
                                  bool eligible, int64_t now_ms);

#endif
