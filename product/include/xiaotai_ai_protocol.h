#ifndef XIAOTAI_AI_PROTOCOL_H
#define XIAOTAI_AI_PROTOCOL_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define XIAOTAI_AI_COMMAND 0x2100U

typedef enum {
    XIAOTAI_AI_MESSAGE_IGNORE = 0,
    XIAOTAI_AI_MESSAGE_CALL_INTENT,
    XIAOTAI_AI_MESSAGE_UI,
    XIAOTAI_AI_MESSAGE_END,
    XIAOTAI_AI_MESSAGE_START_ACCEPTED,
    XIAOTAI_AI_MESSAGE_START_REJECTED,
    XIAOTAI_AI_MESSAGE_INVALID,
} xiaotai_ai_message_t;

typedef struct {
    bool pending;
    bool accepted;
    uint32_t generation;
    uint32_t disconnect_not_before_ms;
    uint32_t deadline_ms;
} xiaotai_ai_end_drain_t;

typedef enum {
    XIAOTAI_AI_END_DRAIN_WAIT = 0,
    XIAOTAI_AI_END_DRAIN_DISCONNECT,
    XIAOTAI_AI_END_DRAIN_STALE,
} xiaotai_ai_end_drain_result_t;

int xiaotai_ai_encode_start(char *output, size_t capacity,
                            const char *request_id, const char *device_id,
                            const char *role_id);

xiaotai_ai_message_t xiaotai_ai_decode_message(const char *json,
                                                size_t length,
                                                const char *request_id);

/** Encode the JSON-RPC response required before committing a device_action
 * call handoff. Returns 0 for legacy call_intent notifications, a positive
 * encoded length for device_action requests, and -1 for invalid input. */
int xiaotai_ai_encode_call_action_result(
    char *output, size_t capacity, const char *request_json, bool ok,
    const char *status, const char *message, const char *contact_type,
    const char *target_device_id, const char *matched_name);

bool xiaotai_ai_accepts_command(uint32_t command, uint32_t response_bit);

void xiaotai_ai_end_drain_begin(xiaotai_ai_end_drain_t *drain,
                                uint32_t generation, uint32_t now_ms,
                                uint32_t arrival_grace_ms,
                                uint32_t timeout_ms);
void xiaotai_ai_end_drain_cancel(xiaotai_ai_end_drain_t *drain);
/** True when this close completes an accepted end_session for this generation. */
bool xiaotai_ai_end_drain_accepts_remote_close(
    const xiaotai_ai_end_drain_t *drain, uint32_t generation);
/** A remote close is normal after end_session or once AI media was active. */
bool xiaotai_ai_remote_close_is_normal(
    const xiaotai_ai_end_drain_t *drain, uint32_t generation,
    bool media_was_active);
xiaotai_ai_end_drain_result_t xiaotai_ai_end_drain_step(
    xiaotai_ai_end_drain_t *drain, uint32_t generation, uint32_t now_ms,
    bool playback_drained);

#endif
