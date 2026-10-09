#ifndef XIAOTAI_METRICS_H
#define XIAOTAI_METRICS_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef struct {
    bool sampled;
    unsigned cpu_percent, task_count, stack_low_words;
    char stack_low_task[24];
    char state[24];
} xiaotai_metrics_snapshot_t;

void xiaotai_metrics_snapshot(xiaotai_metrics_snapshot_t *out);
void xiaotai_metrics_copy_events(char *out, size_t capacity);

/** Start low-rate, structured resource telemetry. */
int xiaotai_metrics_start(void);
/** Update the bounded product-state label included in subsequent snapshots. */
void xiaotai_metrics_set_state(const char *state);
/** Count station link transitions without logging credentials or addresses. */
void xiaotai_metrics_network_event(bool connected);

#endif
