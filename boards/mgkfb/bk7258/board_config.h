#pragma once
/* Declared wiring: MGKFB specification V1.0 pp.12,14,15. HIL pending. */
#define AI_BUTTON GPIO_12
#define VOLUME_UP_BUTTON GPIO_13
#define VOLUME_DOWN_BUTTON GPIO_8
#define PERIPHERAL_POWER_GPIO GPIO_52
#define AMPLIFIER_MUTE_GPIO GPIO_50
#define CAMERA_PWDN_GPIO 0xffU
#define CAMERA_RESET_GPIO GPIO_28
#define CAMERA_SCCB_SCL_GPIO GPIO_42
#define CAMERA_SCCB_SDA_GPIO GPIO_43
/* LCD1=logical left, LCD2=logical right; physical position/orientation needs HIL.
 * Schematic pp.12/15: QSPI1=GPIO2/3/4; QSPI0=GPIO22/23/24. */
#define EYES_LEFT_BUS 1
#define EYES_RIGHT_BUS 0
#define EYES_LEFT_DC_GPIO GPIO_5
#define EYES_LEFT_RESET_GPIO GPIO_45
#define EYES_RIGHT_DC_GPIO GPIO_7
#define EYES_RIGHT_RESET_GPIO GPIO_6
#define EYES_BACKLIGHT_GPIO GPIO_25
