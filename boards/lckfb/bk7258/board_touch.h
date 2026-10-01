#pragma once

#include <stdbool.h>
#include <stdint.h>

#define XIAOTAI_BOARD_TOUCH_AVAILABLE 1

typedef struct {
    uint16_t x;
    uint16_t y;
    bool pressed;
    bool more;
    bool cancelled;
} xiaotai_board_touch_point_t;

int xiaotai_board_touch_open(void);
int xiaotai_board_touch_read(xiaotai_board_touch_point_t *point);
void xiaotai_board_touch_close(void);
