#pragma once
#include <stdbool.h>
#include <stdint.h>
#define XIAOTAI_EYE_WIDTH 160U
#define XIAOTAI_EYE_HEIGHT 160U
/* RGB565 in panel wire byte order; one screen contains exactly one eye. */
void xiaotai_eye_render(uint16_t *pixels, const char *emotion,
                        bool right_eye, uint32_t now_ms);
