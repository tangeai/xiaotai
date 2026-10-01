#ifndef XIAOTAI_TOUCH_H
#define XIAOTAI_TOUCH_H

#include <stdbool.h>
#include <stdint.h>

typedef enum {
    XIAOTAI_TOUCH_DOWN = 0,
    XIAOTAI_TOUCH_MOVE,
    XIAOTAI_TOUCH_UP,
    XIAOTAI_TOUCH_CANCEL,
} xiaotai_touch_event_t;

typedef void (*xiaotai_touch_event_fn)(xiaotai_touch_event_t event,
                                       uint16_t x, uint16_t y,
                                       void *context);

int xiaotai_touch_start(xiaotai_touch_event_fn callback, void *context);

/* Convert FT6336 portrait coordinates through ST7789 MADCTL=MX|MV (0x60)
 * into the LCD's 320x240 landscape framebuffer coordinates. */
bool xiaotai_touch_native_to_landscape(uint16_t raw_x, uint16_t raw_y,
                                       uint16_t *screen_x,
                                       uint16_t *screen_y);

#endif
