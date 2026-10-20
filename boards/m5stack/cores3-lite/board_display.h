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
} cores3_display_t;

esp_err_t cores3_display_init(cores3_display_t *display);
esp_err_t cores3_display_set_awake(bool awake);

/* Compatibility names consumed by the existing shared S3 components. */
#define szpi_board_display_init cores3_display_init
#define szpi_board_display_set_awake cores3_display_set_awake
#define szpi_board_display_t cores3_display_t
