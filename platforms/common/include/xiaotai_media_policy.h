#ifndef XIAOTAI_MEDIA_POLICY_H
#define XIAOTAI_MEDIA_POLICY_H
#include <stdbool.h>
#include <stdint.h>

/* Platform adapters select a resource profile; behavior is shared. Override
 * budgets at compile time only. All clocks are local monotonic milliseconds,
 * never remote/media timestamps. These are initial HIL tuning values. */
#ifndef XIAOTAI_MEDIA_PROFILE
#define XIAOTAI_MEDIA_PROFILE 2 /* S3: LCKFB and CoreS3 Lite */
#endif
#ifndef XIAOTAI_LIVE_RX_MS
#define XIAOTAI_LIVE_RX_MS 160U
#endif
#ifndef XIAOTAI_UPLINK_MAX_AGE_MS
#define XIAOTAI_UPLINK_MAX_AGE_MS 160U
#endif
#ifndef XIAOTAI_AI_PACKET_CAPACITY
#define XIAOTAI_AI_PACKET_CAPACITY 64U
#endif
#ifndef XIAOTAI_UPLINK_SLOTS
#define XIAOTAI_UPLINK_SLOTS 12U
#endif
#define XIAOTAI_LIVE_RX_SLOTS (XIAOTAI_LIVE_RX_MS / 20U)
_Static_assert(XIAOTAI_LIVE_RX_MS >= 20U && XIAOTAI_LIVE_RX_MS % 20U == 0U,
               "live budget must contain whole 20 ms packets");
_Static_assert(XIAOTAI_LIVE_RX_SLOTS <= XIAOTAI_AI_PACKET_CAPACITY &&
               XIAOTAI_AI_PACKET_CAPACITY <= 255U && XIAOTAI_UPLINK_SLOTS <= 255U,
               "queue indices are uint8_t");
/* Policy is selected by business, never by board or resource profile. */
typedef enum {
    XIAOTAI_QUEUE_REJECT_NEW,
    XIAOTAI_QUEUE_REPLACE_OLDEST,
} xiaotai_queue_overflow_t;
static inline xiaotai_queue_overflow_t xiaotai_media_overflow(bool ai_downlink)
{
    return ai_downlink ? XIAOTAI_QUEUE_REJECT_NEW : XIAOTAI_QUEUE_REPLACE_OLDEST;
}
/* Unsigned elapsed time also works across the 32-bit clock rollover. */
static inline bool xiaotai_media_expired(uint32_t now_ms, uint32_t admitted_ms,
                                        uint32_t max_age_ms)
{
    return (uint32_t)(now_ms - admitted_ms) >= max_age_ms;
}
#endif
