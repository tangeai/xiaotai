#pragma once
#include <stdbool.h>
#include <stdint.h>
int xiaotai_board_eyes_init(void);
/* Synchronous ownership: both DMA operations finish before success is returned.
 * On failure, buffers may still be DMA-owned; retain them and stop rendering. */
int xiaotai_board_eyes_present(uint16_t *left, uint16_t *right);
