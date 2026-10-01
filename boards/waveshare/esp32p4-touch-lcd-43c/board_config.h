#pragma once

#include "../../../platforms/esp-idf/waveshare_p4/board_defaults.h"

#define HARDWARE_BOARD_TYPE                     "esp32p4_waveshare_wifi6_touch_lcd_43c"
#define HARDWARE_BOARD_BSP_HEADER               "bsp/esp32_p4_wifi6_touch_lcd_4_3.h"
#define HARDWARE_BOARD_MICROPHONE_CODEC         HARDWARE_AUDIO_CODEC_ES7210
#define HARDWARE_BOARD_MICROPHONE_CODEC_ADDR    ES7210_CODEC_DEFAULT_ADDR
#define HARDWARE_BOARD_MICROPHONE_SELECT_MASK   (ES7210_SEL_MIC1 | ES7210_SEL_MIC2 | \
                                                 ES7210_SEL_MIC3 | ES7210_SEL_MIC4)
#define HARDWARE_BOARD_AI_BUTTON_GPIO           GPIO_NUM_35

/* Product UI runs landscape 800x480 on the rotated 480x800 ST7701 panel. */
#define HARDWARE_BOARD_LCD_WIDTH                800
#define HARDWARE_BOARD_LCD_HEIGHT               480
#define HARDWARE_BOARD_LCD_PHYSICAL_WIDTH       480
#define HARDWARE_BOARD_LCD_PHYSICAL_HEIGHT      800

/* ES8311 DAC-only TX; ES7210 TDM RX carries four slots in the fixed serial
 * order CH1/CH3/CH2/CH4. On this board the onboard mics feed ES7210 MIC2 and
 * the ES8311 speaker output loops back into MIC3 as the AEC reference, so the
 * primary/zero-reference channel pair is (slot 2, slot 1). See the lckfb
 * starter_media ES7210 notes for the equivalent S3 wiring. */
#define HARDWARE_BOARD_AUDIO_ADC_CHANNELS       4
#define HARDWARE_BOARD_AUDIO_ADC_TDM_CHANNELS   4
#define HARDWARE_BOARD_AUDIO_ADC_CHANNEL_MASK   0x0F
#define HARDWARE_BOARD_AUDIO_ADC_PRIMARY_CHANNEL 2
#define HARDWARE_BOARD_AUDIO_ADC_REFERENCE_CHANNEL 1
