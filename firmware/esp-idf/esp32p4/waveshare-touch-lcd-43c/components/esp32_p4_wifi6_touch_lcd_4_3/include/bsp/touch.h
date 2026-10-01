#pragma once
#include "esp_lcd_touch.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Touch configuration.
 *
 * The GT911 interrupt line is not connected on this board; the driver runs in
 * polling mode. Raw coordinates stay in native 480x800 portrait space and the
 * LVGL input device rotation maps them to the logical landscape orientation.
 */
typedef struct {
    unsigned int swap_xy : 1;
    unsigned int mirror_x : 1;
    unsigned int mirror_y : 1;
} bsp_touch_flags_t;

/**
 * @brief Create new touchscreen (GT911, address 0x5D or 0x14).
 *
 * @param[in]  flags     swap/mirror flags for coordinate transformation
 * @param[out] ret_touch esp_lcd_touch touchscreen handle
 * @return
 *      - ESP_OK         On success
 *      - Else           esp_lcd_touch failure
 */
esp_err_t bsp_touch_new(const bsp_touch_flags_t *flags, esp_lcd_touch_handle_t *ret_touch);

#ifdef __cplusplus
}
#endif
