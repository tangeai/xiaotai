#include <assert.h>
#include <stddef.h>
#include <stdint.h>

#include "xiaotai_touch.h"

static void expect_point(uint16_t raw_x, uint16_t raw_y,
                         uint16_t expected_x, uint16_t expected_y)
{
    uint16_t x = UINT16_MAX;
    uint16_t y = UINT16_MAX;
    assert(xiaotai_touch_native_to_landscape(raw_x, raw_y, &x, &y));
    assert(x == expected_x);
    assert(y == expected_y);
}

int main(void)
{
    /* ST7789 MADCTL=MX|MV (0x60): native portrait becomes landscape with
       x=raw_y and y=239-raw_x.  In particular the physical lower-right
       three-dot button must remain in the home menu hit box. */
    expect_point(0, 0, 0, 239);
    expect_point(239, 0, 0, 0);
    expect_point(0, 319, 319, 239);
    expect_point(239, 319, 319, 0);
    expect_point(120, 160, 160, 119);

    uint16_t menu_x = 0;
    uint16_t menu_y = 0;
    assert(xiaotai_touch_native_to_landscape(0, 319, &menu_x, &menu_y));
    assert(menu_x >= 260U && menu_y >= 190U);

    uint16_t x = 0;
    uint16_t y = 0;
    assert(!xiaotai_touch_native_to_landscape(240, 0, &x, &y));
    assert(!xiaotai_touch_native_to_landscape(0, 320, &x, &y));
    assert(!xiaotai_touch_native_to_landscape(0, 0, NULL, &y));
    assert(!xiaotai_touch_native_to_landscape(0, 0, &x, NULL));
    return 0;
}
