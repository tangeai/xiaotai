#pragma once

#include "driver/gpio.h"
#include "driver/i2c_master.h"
#include "driver/i2s_std.h"
#include "driver/spi_master.h"
#include "esp_bit_defs.h"

/* K128-Lite: schematic v1.0, M5Unified and ESP-BSP. See hardware-ir.json. */
#define BOARD_I2C_PORT I2C_NUM_0
#define BOARD_I2C_SDA GPIO_NUM_12
#define BOARD_I2C_SCL GPIO_NUM_11
#define BOARD_I2C_HZ 400000U
#define CAMERA_XCLK GPIO_NUM_NC
#define CAMERA_PCLK GPIO_NUM_45
#define CAMERA_VSYNC GPIO_NUM_46
#define CAMERA_HREF GPIO_NUM_38
#define CAMERA_D0 GPIO_NUM_39
#define CAMERA_D1 GPIO_NUM_40
#define CAMERA_D2 GPIO_NUM_41
#define CAMERA_D3 GPIO_NUM_42
#define CAMERA_D4 GPIO_NUM_15
#define CAMERA_D5 GPIO_NUM_16
#define CAMERA_D6 GPIO_NUM_48
#define CAMERA_D7 GPIO_NUM_47
/* External 24 MHz oscillator; no GPIO-generated XCLK. */
#define CAMERA_XCLK_HZ 24000000U
#define I2S_MCLK GPIO_NUM_0
#define I2S_BCLK GPIO_NUM_34
#define I2S_WS GPIO_NUM_33
/* Schematic names DATI/DATO are from the MCU perspective. */
#define I2S_ADC_DIN GPIO_NUM_14
#define I2S_DAC_DOUT GPIO_NUM_13
#define I2S_AUDIO_PORT I2S_NUM_0
#define AUDIO_TDM_SLOTS 4U
#define AUDIO_HW_SAMPLE_RATE_HZ 16000U
#define AUDIO_I2S_DMA_DESC_NUM 6U
#define AUDIO_I2S_DMA_FRAME_NUM 240U
#define AUDIO_MCLK_MULTIPLE 256U
#define LCD_HOST SPI3_HOST
#define LCD_H_RES 320
#define LCD_V_RES 240
#define LCD_NATIVE_H_RES 320
#define LCD_NATIVE_V_RES 240
/* Camera at the top: rotate native landscape by 180 degrees. */
#define LCD_SWAP_XY false
#define LCD_MIRROR_X true
#define LCD_MIRROR_Y true
#define LCD_PIN_MOSI GPIO_NUM_37
#define LCD_PIN_SCLK GPIO_NUM_36
#define LCD_PIN_DC GPIO_NUM_35
#define LCD_PIN_CS GPIO_NUM_3
#define LCD_DRAW_LINES 10
#define LCD_PIXEL_CLOCK_HZ 40000000U
#define TOUCH_I2C_HZ 400000U
