#include <assert.h>

#include "xiaotai_touch_policy.h"

int main(void)
{
    xiaotai_touch_policy_t policy = {0};
    xiaotai_board_touch_point_t down = {
        .x = 120, .y = 160, .pressed = true,
    };
    xiaotai_touch_policy_result_t result =
        xiaotai_touch_policy_apply(&policy, &down);
    assert(result.emit && result.event == XIAOTAI_TOUCH_DOWN);
    assert(policy.pressed);

    xiaotai_board_touch_point_t failure_release = {
        .x = UINT16_MAX, .y = UINT16_MAX, .cancelled = true,
    };
    result = xiaotai_touch_policy_apply(&policy, &failure_release);
    assert(result.emit && result.event == XIAOTAI_TOUCH_CANCEL);
    assert(!policy.pressed);

    /* Repeated controller failures emit no duplicate cancel/click. */
    result = xiaotai_touch_policy_apply(&policy, &failure_release);
    assert(!result.emit);

    result = xiaotai_touch_policy_apply(&policy, &down);
    assert(result.emit && result.event == XIAOTAI_TOUCH_DOWN);
    xiaotai_board_touch_point_t release = {0};
    result = xiaotai_touch_policy_apply(&policy, &release);
    assert(result.emit && result.event == XIAOTAI_TOUCH_UP);
    return 0;
}
