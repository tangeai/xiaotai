#ifndef XIAOTAI_NETWORK_STATE_H
#define XIAOTAI_NETWORK_STATE_H

#include <stdbool.h>

/* Returns true only when a link notification changes the logical state. */
bool xiaotai_network_link_changed(bool was_ready, bool now_ready);

#endif
