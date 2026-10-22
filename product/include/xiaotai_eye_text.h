#pragma once
#include <stdint.h>
#include <stddef.h>
/* Fixed 16px font, 136px usable width, six 20px rows. No business state. */
size_t xiaotai_eye_text_render(uint16_t *pixels,const char *text,
                              const uint8_t *(*glyph)(uint32_t));
