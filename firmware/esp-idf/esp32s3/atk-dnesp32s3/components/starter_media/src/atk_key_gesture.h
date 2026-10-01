#ifndef ATK_KEY_GESTURE_H
#define ATK_KEY_GESTURE_H

#include <stdbool.h>
#include <stdint.h>

#define ATK_KEY_HOLD_MS 8000

typedef enum {
    ATK_KEY_ACTION_NONE = 0,
    ATK_KEY_ACTION_REPLAY_BINDING,
    ATK_KEY_ACTION_RESET_WIFI,
    ATK_KEY_ACTION_RESET_BINDING,
} atk_key_action_t;

typedef struct {
    uint8_t candidate;
    uint8_t stable;
    uint8_t hold_mask;
    int64_t hold_started_ms;
    bool boot_released;
    bool hold_sent;
} atk_key_gesture_t;

typedef struct {
    bool stable_changed;
    uint8_t stable_mask;
    atk_key_action_t action;
} atk_key_poll_result_t;

/* One event after two equal samples; boot-held buttons must first be released. */
static inline atk_key_poll_result_t atk_key_gesture_poll(atk_key_gesture_t *state,
                                                          uint8_t pressed,
                                                          int64_t now_ms)
{
    atk_key_poll_result_t result = {.stable_mask = state->stable};
    if (pressed == 0U && state->candidate == 0U && state->stable == 0U) {
        state->boot_released = true;
    }
    if (pressed == state->candidate && pressed != state->stable) {
        state->stable = pressed;
        result.stable_changed = true;
        result.stable_mask = pressed;
        if (pressed == 0U) state->boot_released = true;
        if (state->boot_released && (pressed & 0x08U) != 0U) {
            result.action = ATK_KEY_ACTION_REPLAY_BINDING;
        }
    }
    state->candidate = pressed;
    if (state->boot_released &&
        (state->stable == 0x02U || state->stable == 0x01U)) {
        if (state->stable != state->hold_mask) {
            state->hold_mask = state->stable;
            state->hold_started_ms = now_ms;
            state->hold_sent = false;
        } else if (!state->hold_sent &&
                   now_ms - state->hold_started_ms >= ATK_KEY_HOLD_MS) {
            result.action = state->stable == 0x02U
                                ? ATK_KEY_ACTION_RESET_WIFI
                                : ATK_KEY_ACTION_RESET_BINDING;
            state->hold_sent = true;
        }
    } else {
        state->hold_mask = 0;
        state->hold_sent = false;
    }
    return result;
}

#endif
