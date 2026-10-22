#ifndef XIAOTAI_UI_ASSETS_H
#define XIAOTAI_UI_ASSETS_H

#include <stddef.h>
#include <stdint.h>

#define XIAOTAI_WX_QR_MODULES 37U

typedef struct {
    uint32_t codepoint;
    uint8_t bitmap[32];
} xiaotai_cjk_glyph_t;

extern const uint8_t xiaotai_wx_qr_bits[];
extern const xiaotai_cjk_glyph_t xiaotai_cjk_glyphs[];
extern const size_t xiaotai_cjk_glyph_count;

#endif
