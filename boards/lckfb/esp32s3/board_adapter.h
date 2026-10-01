#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "board_bus.h"
#include "esp_err.h"
#include "xiaotai_board_audio.h"
#include "xiaotai_board_camera.h"

/* Media-specific board operations; I2C/selection controls live in board_bus.h. */
esp_err_t szpi_board_camera_init(void);
esp_err_t szpi_board_audio_init(void);
bool szpi_board_audio_ready(void);
xiaotai_board_audio_format_t szpi_board_audio_format(void);
esp_err_t szpi_board_audio_capture(int16_t *samples, size_t bytes);
esp_err_t szpi_board_audio_play(const int16_t *samples, size_t bytes);
esp_err_t szpi_board_audio_set_volume(unsigned percent);
esp_err_t szpi_board_audio_set_muted(bool muted);
const xiaotai_board_audio_adapter_t *szpi_board_audio_adapter(void);
const xiaotai_board_camera_adapter_t *szpi_board_camera_adapter(void);
