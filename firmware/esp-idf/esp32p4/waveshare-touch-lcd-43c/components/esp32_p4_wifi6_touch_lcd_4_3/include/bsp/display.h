#pragma once
#include "esp_lcd_types.h"
#include "esp_lcd_mipi_dsi.h"
#include "sdkconfig.h"

/* LCD color formats */
#define ESP_LCD_COLOR_FORMAT_RGB565    (1)
#define ESP_LCD_COLOR_FORMAT_RGB888    (2)

/* LCD display color format */
#if CONFIG_BSP_LCD_COLOR_FORMAT_RGB888
#define BSP_LCD_COLOR_FORMAT        (ESP_LCD_COLOR_FORMAT_RGB888)
#else
#define BSP_LCD_COLOR_FORMAT        (ESP_LCD_COLOR_FORMAT_RGB565)
#endif
/* LCD display color bytes endianess */
#define BSP_LCD_BIGENDIAN           (0)
/* LCD display color bits */
#define BSP_LCD_BITS_PER_PIXEL      (16)
/* LCD display color space */
#define BSP_LCD_COLOR_SPACE         (ESP_LCD_COLOR_SPACE_RGB)

/*
 * Physical ST7701 panel: 480 wide x 800 tall portrait. The product UI runs
 * landscape 800x480 via LVGL software rotation (see display_driver.c).
 */
#define BSP_LCD_H_RES              (480)
#define BSP_LCD_V_RES              (800)
#define BSP_LCD_MIPI_DSI_LANE_BITRATE_MBPS (500)

#define BSP_LCD_MIPI_DSI_LANE_NUM          (2)    // 2 data lanes
#define BSP_MIPI_DSI_PHY_PWR_LDO_CHAN       (3)  // LDO_VO3 is connected to VDD_MIPI_DPHY
#define BSP_MIPI_DSI_PHY_PWR_LDO_VOLTAGE_MV (2500)

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief BSP display configuration structure
 */
typedef struct {
    int dummy;
} bsp_display_config_t;

/**
 * @brief BSP display return handles
 */
typedef struct {
    esp_lcd_dsi_bus_handle_t    mipi_dsi_bus;  /*!< MIPI DSI bus handle */
    esp_lcd_panel_io_handle_t   io;            /*!< ESP LCD IO handle */
    esp_lcd_panel_handle_t      panel;         /*!< ESP LCD panel (color) handle */
    esp_lcd_panel_handle_t      control;       /*!< ESP LCD panel (control) handle */
} bsp_lcd_handles_t;

/**
 * @brief Create new display panel (reset and initialization only; backlight
 * must be turned on separately via bsp_display_backlight_on()).
 */
esp_err_t bsp_display_new(const bsp_display_config_t *config, esp_lcd_panel_handle_t *ret_panel, esp_lcd_panel_io_handle_t *ret_io);

/**
 * @brief Create new display panel, return all handles in one structure.
 */
esp_err_t bsp_display_new_with_handles(const bsp_display_config_t *config, bsp_lcd_handles_t *ret_handles);

/**
 * @brief Initialize display's brightness (LEDC PWM on the backlight pin).
 */
esp_err_t bsp_display_brightness_init(void);

/**
 * @brief Set display's brightness in [%].
 */
esp_err_t bsp_display_brightness_set(int brightness_percent);

/**
 * @brief Get current brightness in [%].
 */
int bsp_display_brightness_get(void);

/**
 * @brief Turn on display backlight.
 */
esp_err_t bsp_display_backlight_on(void);

/**
 * @brief Turn off display backlight.
 */
esp_err_t bsp_display_backlight_off(void);

#ifdef __cplusplus
}
#endif
