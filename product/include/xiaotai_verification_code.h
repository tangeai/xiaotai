#ifndef XIAOTAI_VERIFICATION_CODE_H
#define XIAOTAI_VERIFICATION_CODE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* Six geometric digits; no font asset, framebuffer or platform dependency. */
typedef void (*xiaotai_code_fill_t)(void *, int, int, int, int);
static inline void xiaotai_verification_code_draw(
    const char *code, int width, int height, xiaotai_code_fill_t fill, void *context)
{
    static const uint8_t rows[10][7] = {
        {14,17,19,21,25,17,14}, {4,12,4,4,4,4,14},
        {14,17,1,2,4,8,31}, {30,1,1,14,1,1,30},
        {2,6,10,18,31,2,2}, {31,16,16,30,1,1,30},
        {14,16,16,30,17,17,14}, {31,1,2,4,8,8,8},
        {14,17,17,14,17,17,14}, {14,17,17,15,1,1,14},
    };
    if (fill == NULL || width < 35 || height < 7) return;
    bool valid = code != NULL;
    for (unsigned i = 0; valid && i < 6U; ++i)
        valid = code[i] >= '0' && code[i] <= '9';
    if (valid) valid = code[6] == '\0';
    int scale = (width - 16) / 35;
    int vertical_scale = (height - 16) / 7;
    if (scale > vertical_scale) scale = vertical_scale;
    if (scale < 1) scale = 1;
    int left = (width - 35 * scale) / 2;
    int top = (height - 7 * scale) / 2;
    for (unsigned digit = 0; digit < 6U; ++digit) {
        for (unsigned row = 0; row < 7U; ++row) {
            unsigned mask = valid ? rows[code[digit] - '0'][row] :
                                   (row == 3U ? 31U : 0U);
            for (unsigned col = 0; col < 5U; ++col) {
                if ((mask & (1U << (4U - col))) != 0U)
                    fill(context, left + ((int)digit * 6 + (int)col) * scale,
                         top + (int)row * scale, scale, scale);
            }
        }
    }
}
#endif
