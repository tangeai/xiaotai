#pragma once
#include <stdbool.h>
#include <common/bk_err.h>
bk_err_t xiaotai_board_button_init(void);
bool xiaotai_board_button_released(void);
/* Key order: primary S2, volume-up S1, volume-down S3. */
bool xiaotai_board_key_released(unsigned index);
