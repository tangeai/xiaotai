#ifndef XIAOTAI_IDENTITY_GATE_H
#define XIAOTAI_IDENTITY_GATE_H

#include <stdatomic.h>
#include <stdbool.h>

/* Serialize identity writers. Successful manual recovery excludes background
 * writes until reboot, but permits another manual attempt if reboot failed.
 * Never spin or hold an SDK critical section across flash operations. */
typedef struct { atomic_uint state; } xiaotai_identity_gate_t;
enum { XIAOTAI_IDENTITY_IDLE, XIAOTAI_IDENTITY_WRITING,
       XIAOTAI_IDENTITY_RECOVERING, XIAOTAI_IDENTITY_FROZEN };

static inline bool xiaotai_identity_write_begin(xiaotai_identity_gate_t *gate)
{
    unsigned expected = XIAOTAI_IDENTITY_IDLE;
    return atomic_compare_exchange_strong(&gate->state, &expected, XIAOTAI_IDENTITY_WRITING);
}

static inline void xiaotai_identity_write_end(xiaotai_identity_gate_t *gate)
{
    atomic_store(&gate->state, XIAOTAI_IDENTITY_IDLE);
}

static inline int xiaotai_identity_recovery_begin(xiaotai_identity_gate_t *gate)
{
    unsigned expected = atomic_load(&gate->state);
    if (expected != XIAOTAI_IDENTITY_IDLE && expected != XIAOTAI_IDENTITY_FROZEN) return -1;
    unsigned previous = expected;
    if (!atomic_compare_exchange_strong(&gate->state, &expected, XIAOTAI_IDENTITY_RECOVERING)) return -1;
    return (int)previous;
}

static inline void xiaotai_identity_recovery_end(xiaotai_identity_gate_t *gate,
                                                int previous, bool success)
{
    atomic_store(&gate->state, success ? XIAOTAI_IDENTITY_FROZEN : (unsigned)previous);
}
#endif
