#include "xiaotai_touch_policy.h"

#include <stddef.h>

xiaotai_touch_policy_result_t xiaotai_touch_policy_apply(
    xiaotai_touch_policy_t *policy,
    const xiaotai_board_touch_point_t *point)
{
    xiaotai_touch_policy_result_t result = {0};
    if (policy == NULL || point == NULL) return result;

    if (point->pressed) {
        uint16_t x = 0;
        uint16_t y = 0;
        if (!xiaotai_touch_native_to_landscape(point->x, point->y, &x, &y)) {
            return result;
        }
        result.emit = true;
        result.event = policy->pressed ? XIAOTAI_TOUCH_MOVE :
                                         XIAOTAI_TOUCH_DOWN;
        result.x = x;
        result.y = y;
        policy->pressed = true;
        policy->last_x = x;
        policy->last_y = y;
        return result;
    }

    if (!policy->pressed) return result;
    policy->pressed = false;
    result.emit = true;
    result.event = point->cancelled ? XIAOTAI_TOUCH_CANCEL : XIAOTAI_TOUCH_UP;
    result.x = policy->last_x;
    result.y = policy->last_y;
    return result;
}
