#ifndef XIAOTAI_BOARD_BUTTON_H
#define XIAOTAI_BOARD_BUTTON_H

#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/** Platform-neutral, active-state-normalized user button operations. */
typedef struct {
    void *context;
    const char *name;
    int (*initialize)(void *context);
    bool (*read_pressed)(void *context);
} xiaotai_board_button_adapter_t;

/** Return the single user-button adapter selected by the board build. */
const xiaotai_board_button_adapter_t *xiaotai_board_button_adapter(void);

#ifdef __cplusplus
}
#endif

#endif
