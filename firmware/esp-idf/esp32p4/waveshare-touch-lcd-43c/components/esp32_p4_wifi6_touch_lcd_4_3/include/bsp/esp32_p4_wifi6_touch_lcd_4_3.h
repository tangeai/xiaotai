#pragma once

#include "sdkconfig.h"
#include "driver/gpio.h"
#include "driver/i2c_master.h"
#include "bsp/config.h"
#include "bsp/display.h"
#include "bsp/touch.h"

#ifdef __cplusplus
extern "C" {
#endif

/**************************************************************************************************
 *  BSP Capabilities
 **************************************************************************************************/

#define BSP_CAPS_DISPLAY        1
#define BSP_CAPS_TOUCH          1
#define BSP_CAPS_BUTTONS        0
#define BSP_CAPS_AUDIO          1
#define BSP_CAPS_AUDIO_SPEAKER  1
#define BSP_CAPS_AUDIO_MIC      1
#define BSP_CAPS_SDCARD         1
#define BSP_CAPS_IMU            0

/**************************************************************************************************
 *  Waveshare ESP32-P4-WIFI6-Touch-LCD-4.3(-C) pinout
 *
 *  Values corroborated against the board schematic and the official BSP v1.0.1:
 *  - I2C1 (SDA=7, SCL=8) carries ES8311, ES7210, GT911 and the OV5647 SCCB.
 *  - LCD: ST7701 MIPI-DSI 2 lanes; reset GPIO27; backlight GPIO26 (LEDC,
 *    inverted output). GT911 touch has no reset/int line on this PCB.
 *  - Audio I2S1: MCLK=13, BCLK=12, WS=10, DOUT=9, DIN=11; speaker PA on GPIO53.
 **************************************************************************************************/

/* I2C */
#define BSP_I2C_SCL           (GPIO_NUM_8)
#define BSP_I2C_SDA           (GPIO_NUM_7)
#define BSP_I2C_NUM           CONFIG_BSP_I2C_NUM

/* Audio (used by main/drivers/audio; kept here as the board wiring record) */
#define BSP_I2S_SCLK          (GPIO_NUM_12)
#define BSP_I2S_MCLK          (GPIO_NUM_13)
#define BSP_I2S_LCLK          (GPIO_NUM_10)
#define BSP_I2S_DOUT          (GPIO_NUM_9)
#define BSP_I2S_DSIN          (GPIO_NUM_11)
#define BSP_POWER_AMP_IO      (GPIO_NUM_53)

/* LCD */
#define BSP_LCD_BACKLIGHT     (GPIO_NUM_26)
#define BSP_LCD_RST           (GPIO_NUM_27)

/* Touch: GT911 reset and interrupt are not connected on this PCB revision
 * (they are absent from the schematic; polling mode via I2C). */
#define BSP_LCD_TOUCH_RST     (GPIO_NUM_NC)
#define BSP_LCD_TOUCH_INT     (GPIO_NUM_NC)

/* uSD card (Slot 0 IO MUX pins, on-chip LDO channel 4 power control) */
#define BSP_SD_D0             (GPIO_NUM_39)
#define BSP_SD_D1             (GPIO_NUM_40)
#define BSP_SD_D2             (GPIO_NUM_41)
#define BSP_SD_D3             (GPIO_NUM_42)
#define BSP_SD_CMD            (GPIO_NUM_44)
#define BSP_SD_CLK            (GPIO_NUM_43)

/* Board buttons: K1=POWER, K2/K3 spare (see starter button wiring). BOOT is
 * the GPIO35 download strap; RESET is the chip EN line. */
#define BSP_BUTTON_POWER_GPIO (GPIO_NUM_21)
#define BSP_BUTTON_K2_GPIO    (GPIO_NUM_23)
#define BSP_BUTTON_K3_GPIO    (GPIO_NUM_28)
#define BSP_BATTERY_ADC_GPIO  (GPIO_NUM_20)

/**************************************************************************************************
 *  I2C interface
 *
 *  Multiple devices on one bus: ES8311 + ES7210 (configuration), GT911 touch,
 *  OV5647 camera SCCB (via esp_video, init_sccb=false).
 **************************************************************************************************/

/**
 * @brief Init I2C driver (idempotent).
 */
esp_err_t bsp_i2c_init(void);

/**
 * @brief Deinit I2C driver and free its resources.
 */
esp_err_t bsp_i2c_deinit(void);

/**
 * @brief Get I2C bus handle (call bsp_i2c_init() first).
 */
i2c_master_bus_handle_t bsp_i2c_get_handle(void);

/**
 * @brief Get LCD panel handle.
 */
esp_lcd_panel_handle_t bsp_display_get_panel_handle(void);

/**
 * @brief Get LCD panel IO handle.
 */
esp_lcd_panel_io_handle_t bsp_display_get_io_handle(void);

#ifdef __cplusplus
}
#endif
