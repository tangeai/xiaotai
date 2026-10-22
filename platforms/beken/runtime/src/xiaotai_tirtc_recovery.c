#include "xiaotai_tirtc_recovery.h"

#include <stddef.h>

#define RECOVERY_STAGE_TIMEOUT_MS 5000U

static bool deadline_reached(uint32_t now_ms, uint32_t deadline_ms)
{
    return (int32_t)(now_ms - deadline_ms) >= 0;
}

void xiaotai_tirtc_recovery_init(xiaotai_tirtc_recovery_t *recovery)
{
    if (recovery == NULL) return;
    recovery->phase = XIAOTAI_TIRTC_RECOVERY_IDLE;
    recovery->deadline_ms = 0U;
}

void xiaotai_tirtc_recovery_request(xiaotai_tirtc_recovery_t *recovery)
{
    if (recovery != NULL && recovery->phase == XIAOTAI_TIRTC_RECOVERY_IDLE) {
        recovery->phase = XIAOTAI_TIRTC_RECOVERY_REQUESTED;
    }
}

bool xiaotai_tirtc_recovery_heap_leaked(uint32_t baseline_bytes,
                                        uint32_t current_bytes,
                                        uint32_t allowed_retained_bytes)
{
    return baseline_bytes > current_bytes &&
           baseline_bytes - current_bytes > allowed_retained_bytes;
}

bool xiaotai_tirtc_transport_quiet_elapsed(uint32_t closed_ms,
                                           uint32_t now_ms,
                                           uint32_t quiet_ms)
{
    return (uint32_t)(now_ms - closed_ms) >= quiet_ms;
}

xiaotai_tirtc_recovery_action_t xiaotai_tirtc_recovery_step(
    xiaotai_tirtc_recovery_t *recovery,
    bool adapter_requests_recovery,
    xiaotai_tirtc_recovery_sdk_state_t sdk_state,
    uint32_t now_ms)
{
    if (recovery == NULL) return XIAOTAI_TIRTC_RECOVERY_REBOOT;
    if (recovery->phase == XIAOTAI_TIRTC_RECOVERY_IDLE) {
        if (!adapter_requests_recovery) return XIAOTAI_TIRTC_RECOVERY_NONE;
        recovery->phase = XIAOTAI_TIRTC_RECOVERY_REQUESTED;
    }
    if (recovery->phase == XIAOTAI_TIRTC_RECOVERY_REQUESTED) {
        recovery->deadline_ms = now_ms + RECOVERY_STAGE_TIMEOUT_MS;
        if (sdk_state == XIAOTAI_TIRTC_RECOVERY_SDK_STOPPED) {
            recovery->phase = XIAOTAI_TIRTC_RECOVERY_WAIT_START;
            return XIAOTAI_TIRTC_RECOVERY_RESTART;
        }
        recovery->phase = XIAOTAI_TIRTC_RECOVERY_WAIT_STOP;
        return XIAOTAI_TIRTC_RECOVERY_STOP;
    }
    if (recovery->phase == XIAOTAI_TIRTC_RECOVERY_WAIT_STOP) {
        if (sdk_state == XIAOTAI_TIRTC_RECOVERY_SDK_STOPPED) {
            recovery->phase = XIAOTAI_TIRTC_RECOVERY_WAIT_START;
            recovery->deadline_ms = now_ms + RECOVERY_STAGE_TIMEOUT_MS;
            return XIAOTAI_TIRTC_RECOVERY_RESTART;
        }
        if (sdk_state == XIAOTAI_TIRTC_RECOVERY_SDK_FAILED ||
            deadline_reached(now_ms, recovery->deadline_ms)) {
            return XIAOTAI_TIRTC_RECOVERY_REBOOT;
        }
        return XIAOTAI_TIRTC_RECOVERY_NONE;
    }
    if (sdk_state == XIAOTAI_TIRTC_RECOVERY_SDK_READY) {
        recovery->phase = XIAOTAI_TIRTC_RECOVERY_IDLE;
        recovery->deadline_ms = 0U;
        return XIAOTAI_TIRTC_RECOVERY_COMPLETE;
    }
    if (sdk_state == XIAOTAI_TIRTC_RECOVERY_SDK_FAILED ||
        deadline_reached(now_ms, recovery->deadline_ms)) {
        return XIAOTAI_TIRTC_RECOVERY_REBOOT;
    }
    return XIAOTAI_TIRTC_RECOVERY_NONE;
}
