#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "board_touch.h"
#include "xiaotai_touch.h"

typedef struct {
    bool pressed;
    uint16_t last_x;
    uint16_t last_y;
} xiaotai_touch_policy_t;

typedef struct {
    bool emit;
    xiaotai_touch_event_t event;
    uint16_t x;
    uint16_t y;
} xiaotai_touch_policy_result_t;

xiaotai_touch_policy_result_t xiaotai_touch_policy_apply(
    xiaotai_touch_policy_t *policy,
    const xiaotai_board_touch_point_t *point);
