#pragma once
#include <stdbool.h>
#include <stdint.h>
typedef enum { XIAOTAI_KEY_NONE, XIAOTAI_KEY_SHORT, XIAOTAI_KEY_DOUBLE,
               XIAOTAI_KEY_LONG } xiaotai_key_event_t;
typedef struct {
    bool initialized, stable, candidate, armed, long_sent, pending;
    unsigned stable_polls;
    uint32_t pressed_at, released_at;
} xiaotai_key_gesture_t;
/* Sample every 20 ms; three equal samples debounce, 320 ms double window,
 * 700 ms long. A key held at boot must be released before any action. */
xiaotai_key_event_t xiaotai_key_sample(xiaotai_key_gesture_t *key,
                                     bool released, uint32_t now);

/* Volume-style key: report a short press on debounced release immediately.
 * The same boot guard and long-press rule apply; never merge two clicks. */
xiaotai_key_event_t xiaotai_key_sample_single(xiaotai_key_gesture_t *key,
                                            bool released, uint32_t now);
