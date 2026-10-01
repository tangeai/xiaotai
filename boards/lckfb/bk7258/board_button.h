#pragma once

#include <stdbool.h>

#include <common/bk_err.h>

bk_err_t xiaotai_board_button_init(void);
bool xiaotai_board_button_released(void);
