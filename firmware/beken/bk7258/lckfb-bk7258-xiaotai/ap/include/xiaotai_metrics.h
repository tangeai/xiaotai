#ifndef XIAOTAI_METRICS_H
#define XIAOTAI_METRICS_H

#include <stdbool.h>

/** Start low-rate, structured resource telemetry. */
int xiaotai_metrics_start(void);
/** Update the bounded product-state label included in subsequent snapshots. */
void xiaotai_metrics_set_state(const char *state);
/** Count station link transitions without logging credentials or addresses. */
void xiaotai_metrics_network_event(bool connected);

#endif
