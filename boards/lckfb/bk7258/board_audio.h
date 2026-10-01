#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include <common/bk_err.h>

#include "xiaotai_board_audio.h"

const xiaotai_board_audio_adapter_t *xiaotai_board_audio_adapter(void);

bk_err_t xiaotai_board_prompt_audio_start(void);
int xiaotai_board_prompt_audio_write(const int16_t *samples, size_t frames);
void xiaotai_board_prompt_audio_stop(void);

bk_err_t xiaotai_board_amplifier_idle(void);
bk_err_t xiaotai_board_amplifier_prepare(void);
bk_err_t xiaotai_board_amplifier_set_playing(bool playing);
void xiaotai_board_amplifier_power_off(void);
bool xiaotai_board_amplifier_prepared(void);
