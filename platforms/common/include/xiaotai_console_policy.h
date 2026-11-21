#ifndef XIAOTAI_CONSOLE_POLICY_H
#define XIAOTAI_CONSOLE_POLICY_H

#include <stdbool.h>
#include <stddef.h>
#include <string.h>

/* These commands only use persistent configuration or restart APIs.
 * Status has a separate minimal response until the local runtime is ready. */
static inline bool xiaotai_console_recovery_command(const char *name)
{
    static const char *const commands[] = {
        "version", "wifi-set", "wifi-clear", "wificlear", "tirtc-set", "tirtc-clear",
        "ntp-show", "ntp-set", "ntp-reset", "ep-show", "ep-set", "ep-reset",
        "restart"
    };
    if (name == NULL) return false;
    for (size_t i = 0; i < sizeof(commands)/sizeof(commands[0]); ++i)
        if (strcmp(name, commands[i]) == 0) return true;
    return false;
}

#endif
