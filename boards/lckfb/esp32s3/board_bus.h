#pragma once

#include <stdbool.h>

#include "driver/i2c_master.h"
#include "esp_err.h"

esp_err_t szpi_board_init(void);
i2c_master_bus_handle_t szpi_board_i2c_bus(void);
esp_err_t szpi_board_select_lcd(bool selected);
esp_err_t szpi_board_power_camera(bool enabled);
esp_err_t szpi_board_power_amplifier(bool enabled);
