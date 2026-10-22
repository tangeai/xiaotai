#ifndef XIAOTAI_BUTTON_H
#define XIAOTAI_BUTTON_H

typedef enum {
    XIAOTAI_BUTTON_DOWN = 1,
    XIAOTAI_BUTTON_UP,
    XIAOTAI_BUTTON_SHORT,
    XIAOTAI_BUTTON_DOUBLE,
    XIAOTAI_BUTTON_LONG,
    XIAOTAI_BUTTON_VOLUME_UP,
    XIAOTAI_BUTTON_VOLUME_DOWN,
    XIAOTAI_BUTTON_PROVISION,
} xiaotai_button_event_t;

typedef void (*xiaotai_button_event_fn)(xiaotai_button_event_t event,
                                        void *context);

/**
 * Start the board-selected active-low key worker.
 * DOWN/UP are immediate debounced edges. SHORT is delayed for the double-click
 * window for the primary key; volume keys report on release without merging.
 * LONG is emitted once while the key remains held.
 */
int xiaotai_button_start(xiaotai_button_event_fn callback, void *context);

#endif
