#include <assert.h>
#include <stdint.h>

#include "atk_key_gesture.h"

static atk_key_action_t poll_twice(atk_key_gesture_t *state, uint8_t mask,
                                   int64_t first_ms)
{
    (void)atk_key_gesture_poll(state, mask, first_ms);
    return atk_key_gesture_poll(state, mask, first_ms + 30).action;
}

int main(void)
{
    atk_key_gesture_t state = {0};
    /* Boot-held reset cannot clear credentials. */
    assert(poll_twice(&state, 0x02, 0) == ATK_KEY_ACTION_NONE);
    assert(atk_key_gesture_poll(&state, 0x02, 9000).action == ATK_KEY_ACTION_NONE);
    assert(poll_twice(&state, 0, 9100) == ATK_KEY_ACTION_NONE);
    /* One bouncing sample does not arm a reset. */
    assert(atk_key_gesture_poll(&state, 0x02, 9200).action == ATK_KEY_ACTION_NONE);
    assert(atk_key_gesture_poll(&state, 0, 9230).action == ATK_KEY_ACTION_NONE);
    assert(poll_twice(&state, 0x02, 9300) == ATK_KEY_ACTION_NONE);
    assert(atk_key_gesture_poll(&state, 0x02, 17320).action == ATK_KEY_ACTION_NONE);
    assert(atk_key_gesture_poll(&state, 0x02, 17330).action == ATK_KEY_ACTION_RESET_WIFI);
    assert(atk_key_gesture_poll(&state, 0x02, 22000).action == ATK_KEY_ACTION_NONE);
    assert(poll_twice(&state, 0, 22100) == ATK_KEY_ACTION_NONE);
    assert(poll_twice(&state, 0x01, 22200) == ATK_KEY_ACTION_NONE);
    assert(atk_key_gesture_poll(&state, 0x01, 30230).action == ATK_KEY_ACTION_RESET_BINDING);
    assert(poll_twice(&state, 0, 30300) == ATK_KEY_ACTION_NONE);
    assert(poll_twice(&state, 0x08, 30400) == ATK_KEY_ACTION_REPLAY_BINDING);
    assert(atk_key_gesture_poll(&state, 0x08, 40000).action == ATK_KEY_ACTION_NONE);
    return 0;
}
