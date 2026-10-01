#include "xiaotai_touch.h"

#include <stddef.h>

bool xiaotai_touch_native_to_landscape(uint16_t raw_x, uint16_t raw_y,
                                       uint16_t *screen_x,
                                       uint16_t *screen_y)
{
    if (screen_x == NULL || screen_y == NULL ||
        raw_x >= 240U || raw_y >= 320U) {
        return false;
    }

    /* ST7789V2 MADCTL=0x60 sets MX|MV.  The LCKFB portrait touch demo proves
     * FT6336 native coordinates directly; applying the display transform for
     * the product's landscape framebuffer gives x=raw_y, y=239-raw_x. */
    *screen_x = raw_y;
    *screen_y = (uint16_t)(239U - raw_x);
    return true;
}
