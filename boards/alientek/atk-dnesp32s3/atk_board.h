#ifndef ATK_BOARD_H
#define ATK_BOARD_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"
#include "xiaotai_board_audio.h"
#include "xiaotai_board_camera.h"

/* ATK-DNESP32S3 V1.4 candidate wiring; probe failures disable the path. */
esp_err_t atk_board_init(void);
bool atk_board_audio_ready(void);
xiaotai_board_audio_format_t atk_board_audio_format(void);
bool atk_board_camera_ready(void);
esp_err_t atk_board_audio_read(int16_t *stereo, size_t frames, size_t *bytes_read);
esp_err_t atk_board_audio_write(const int16_t *stereo, size_t frames, size_t *bytes_written);
esp_err_t atk_board_keys(uint8_t *pressed_mask);
const xiaotai_board_audio_adapter_t *atk_board_audio_adapter(void);
const xiaotai_board_camera_adapter_t *atk_board_camera_adapter(void);

#endif
