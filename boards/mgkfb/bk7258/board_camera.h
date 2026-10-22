#pragma once

#include <common/bk_err.h>
#include <components/dvp_camera_types.h>

/** Reserve the board's DVP/H.264 controller while contiguous SRAM is available. */
bk_err_t xiaotai_board_camera_reserve(const bk_dvp_callback_t *callbacks);
bk_err_t xiaotai_board_camera_start(void);
void xiaotai_board_camera_stop(void);

