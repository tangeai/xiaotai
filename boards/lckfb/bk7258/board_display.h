#pragma once

#include <stdbool.h>
#include <stdint.h>

#include <common/bk_err.h>

#include "frame_buffer.h"

#define XIAOTAI_BOARD_DISPLAY_WIDTH 320U
#define XIAOTAI_BOARD_DISPLAY_HEIGHT 240U

bk_err_t xiaotai_board_display_init(void);
bool xiaotai_board_display_ready(void);
bk_err_t xiaotai_board_display_flush(frame_buffer_t *frame,
                                     bk_err_t (*release)(void *arg));
bk_err_t xiaotai_board_display_flush_tagged(frame_buffer_t *frame,
                                            bk_err_t (*release)(void *arg),
                                            uint32_t *generation_out);
/** Queue an RGB565 update; rejects a region after another full page draw. */
bk_err_t xiaotai_board_display_flush_region(uint16_t x, uint16_t y,
                                            uint16_t width, uint16_t height,
                                            uint8_t *pixels,
                                            uint32_t expected_generation);
uint32_t xiaotai_board_display_generation(void);
bk_err_t xiaotai_board_display_set_backlight(bool enabled);
bool xiaotai_board_display_backlight_on(void);
const void *xiaotai_board_display_probe_lcd(void);
