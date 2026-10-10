#ifndef STARTER_SEND_LOG_H
#define STARTER_SEND_LOG_H

#include <stdint.h>
#include <stdbool.h>

/* Owned by one sender task. Count every failure, print at most once per 5 s.
 * Millisecond subtraction also handles the monotonic 32-bit clock wrapping. */
typedef struct {
    uint32_t generation, failures, last_log_ms;
    bool failing;
} starter_send_log_t;

static inline void starter_send_log_record(starter_send_log_t *state,
    const char *tag, uint32_t generation, int mode, int result, uint32_t now_ms)
{
    if (state->generation != generation) {
        *state = (starter_send_log_t){.generation = generation};
    }
    if (result >= 0) {
        if (state->failing) {
            ESP_LOGI(tag, "uplink send recovered mode=%d generation=%lu failures=%lu",
                mode, (unsigned long)generation, (unsigned long)state->failures);
        }
        state->failing = false;
        return;
    }
    if (state->failures != UINT32_MAX) ++state->failures;
    if (!state->failing || (uint32_t)(now_ms - state->last_log_ms) >= 5000U) {
        ESP_LOGW(tag, "uplink send failed mode=%d generation=%lu ret=%d failures=%lu",
            mode, (unsigned long)generation, result, (unsigned long)state->failures);
        state->last_log_ms = now_ms;
    }
    state->failing = true;
    (void)tag;
    (void)mode;
}
#endif
