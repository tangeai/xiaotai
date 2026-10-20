#pragma once

#include <stdbool.h>

#include "driver/i2c_master.h"
#include "esp_err.h"

esp_err_t cores3_init(void);
i2c_master_bus_handle_t cores3_i2c_bus(void);
esp_err_t cores3_select_lcd(bool selected);
esp_err_t cores3_power_camera(bool enabled);
esp_err_t cores3_power_amplifier(bool enabled);

/* Compatibility names consumed by the existing shared S3 components. */
#define szpi_board_i2c_bus cores3_i2c_bus
#define szpi_board_init cores3_init
#define szpi_board_power_amplifier cores3_power_amplifier
#define szpi_board_power_camera cores3_power_camera
#define szpi_board_select_lcd cores3_select_lcd
