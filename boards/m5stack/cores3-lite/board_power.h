#pragma once
#include <stdbool.h>
#include <stdint.h>
#include "driver/i2c_master.h"
#include "esp_err.h"
esp_err_t cores3_board_init(i2c_master_bus_handle_t bus);
esp_err_t cores3_board_lcd_reset(void);
esp_err_t cores3_board_backlight(bool enabled);
esp_err_t cores3_board_camera_power(bool enabled);
esp_err_t cores3_board_power_key(uint8_t *press);
