#pragma once

#include <stdbool.h>

#include <common/bk_err.h>

#include "frame_buffer.h"

#define XIAOTAI_BOARD_DISPLAY_WIDTH 320U
#define XIAOTAI_BOARD_DISPLAY_HEIGHT 240U

bk_err_t xiaotai_board_display_init(void);
bool xiaotai_board_display_ready(void);
bk_err_t xiaotai_board_display_flush(frame_buffer_t *frame,
                                     bk_err_t (*release)(void *arg));
bk_err_t xiaotai_board_display_set_backlight(bool enabled);
bool xiaotai_board_display_backlight_on(void);
const void *xiaotai_board_display_probe_lcd(void);
