#ifndef XIAOTAI_TIRTC_RECOVERY_H
#define XIAOTAI_TIRTC_RECOVERY_H

#include <stdbool.h>
#include <stdint.h>

typedef enum {
    XIAOTAI_TIRTC_RECOVERY_SDK_STOPPED = 0,
    XIAOTAI_TIRTC_RECOVERY_SDK_STARTING,
    XIAOTAI_TIRTC_RECOVERY_SDK_READY,
    XIAOTAI_TIRTC_RECOVERY_SDK_STOPPING,
    XIAOTAI_TIRTC_RECOVERY_SDK_FAILED,
} xiaotai_tirtc_recovery_sdk_state_t;

typedef enum {
    XIAOTAI_TIRTC_RECOVERY_NONE = 0,
    XIAOTAI_TIRTC_RECOVERY_STOP,
    XIAOTAI_TIRTC_RECOVERY_RESTART,
    XIAOTAI_TIRTC_RECOVERY_COMPLETE,
    XIAOTAI_TIRTC_RECOVERY_REBOOT,
} xiaotai_tirtc_recovery_action_t;

typedef enum {
    XIAOTAI_TIRTC_RECOVERY_IDLE = 0,
    XIAOTAI_TIRTC_RECOVERY_REQUESTED,
    XIAOTAI_TIRTC_RECOVERY_WAIT_STOP,
    XIAOTAI_TIRTC_RECOVERY_WAIT_START,
} xiaotai_tirtc_recovery_phase_t;

typedef struct {
    xiaotai_tirtc_recovery_phase_t phase;
    uint32_t deadline_ms;
} xiaotai_tirtc_recovery_t;

void xiaotai_tirtc_recovery_init(xiaotai_tirtc_recovery_t *recovery);
void xiaotai_tirtc_recovery_request(xiaotai_tirtc_recovery_t *recovery);
bool xiaotai_tirtc_recovery_heap_leaked(uint32_t baseline_bytes,
                                        uint32_t current_bytes,
                                        uint32_t allowed_retained_bytes);
bool xiaotai_tirtc_transport_quiet_elapsed(uint32_t closed_ms,
                                           uint32_t now_ms,
                                           uint32_t quiet_ms);
xiaotai_tirtc_recovery_action_t xiaotai_tirtc_recovery_step(
    xiaotai_tirtc_recovery_t *recovery,
    bool adapter_requests_recovery,
    xiaotai_tirtc_recovery_sdk_state_t sdk_state,
    uint32_t now_ms);

#endif
