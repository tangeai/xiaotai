#pragma once

#include <stdbool.h>

#include "esp_err.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_touch.h"

typedef struct {
    esp_lcd_panel_io_handle_t io;
    esp_lcd_panel_handle_t panel;
    esp_lcd_touch_handle_t touch;
} szpi_board_display_t;

esp_err_t szpi_board_display_init(szpi_board_display_t *display);
esp_err_t szpi_board_display_set_awake(bool awake);
