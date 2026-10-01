#ifndef XIAOTAI_BUTTON_H
#define XIAOTAI_BUTTON_H

typedef enum {
    XIAOTAI_BUTTON_DOWN = 1,
    XIAOTAI_BUTTON_UP,
    XIAOTAI_BUTTON_SHORT,
    XIAOTAI_BUTTON_DOUBLE,
    XIAOTAI_BUTTON_LONG,
} xiaotai_button_event_t;

typedef void (*xiaotai_button_event_fn)(xiaotai_button_event_t event,
                                        void *context);

/**
 * Start the board KEY (GPIO7, active-low) worker.
 * DOWN/UP are immediate debounced edges. SHORT is delayed for the double-click
 * window; LONG is emitted once while the key remains held.
 */
int xiaotai_button_start(xiaotai_button_event_fn callback, void *context);

#endif
