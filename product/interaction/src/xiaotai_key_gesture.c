#include "xiaotai_key_gesture.h"

xiaotai_key_event_t xiaotai_key_sample(xiaotai_key_gesture_t *k,
                                     bool released, uint32_t now)
{
    if (!k) return XIAOTAI_KEY_NONE;
    if (!k->initialized) {
        k->initialized = true;
        k->stable = k->candidate = released;
        k->armed = released;
        return XIAOTAI_KEY_NONE;
    }
    if (released != k->candidate) { k->candidate = released; k->stable_polls = 1; }
    else if (k->stable_polls < 3) ++k->stable_polls;
    if (k->stable_polls == 3 && k->stable != k->candidate) {
        k->stable = k->candidate;
        if (!k->armed) {
            if (released) k->armed = true;
            return XIAOTAI_KEY_NONE;
        }
        if (!released) { k->pressed_at = now; k->long_sent = false; }
        else if (!k->long_sent) {
            if (k->pending && (uint32_t)(now-k->released_at) <= 320U) {
                k->pending = false;
                return XIAOTAI_KEY_DOUBLE;
            }
            k->pending = true; k->released_at = now;
        }
    }
    if (!k->armed) return XIAOTAI_KEY_NONE;
    if (!k->stable && !k->long_sent && (uint32_t)(now-k->pressed_at) >= 700U) {
        k->long_sent = true; k->pending = false;
        return XIAOTAI_KEY_LONG;
    }
    if (k->stable && k->pending && (uint32_t)(now-k->released_at) > 320U) {
        k->pending = false;
        return XIAOTAI_KEY_SHORT;
    }
    return XIAOTAI_KEY_NONE;
}
