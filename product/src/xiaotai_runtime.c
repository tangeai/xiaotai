#include "xiaotai_runtime.h"

#include <stddef.h>

static bool valid_owner(xiaotai_session_owner_t owner)
{
    return owner >= XIAOTAI_OWNER_STREAM && owner <= XIAOTAI_OWNER_ROOM;
}

void xiaotai_runtime_init(xiaotai_runtime_t *runtime)
{
    if (runtime == NULL) return;
    runtime->owner = XIAOTAI_OWNER_NONE;
    runtime->state = XIAOTAI_STATE_WAITING;
    runtime->generation = 0;
    runtime->deadline_ms = 0;
    runtime->pending_incoming = XIAOTAI_OWNER_NONE;
    runtime->pending_generation = 0;
    runtime->pending_deadline_ms = 0;
}

bool xiaotai_runtime_begin(xiaotai_runtime_t *runtime,
                           xiaotai_session_owner_t owner,
                           bool incoming)
{
    if (runtime == NULL || !valid_owner(owner) ||
        runtime->owner != XIAOTAI_OWNER_NONE) {
        return false;
    }

    runtime->generation++;
    if (runtime->generation == 0) runtime->generation = 1;
    runtime->owner = owner;
    runtime->deadline_ms = 0;

    if (owner == XIAOTAI_OWNER_STREAM) {
        runtime->state = XIAOTAI_STATE_STREAM_ACTIVE;
    } else if (owner == XIAOTAI_OWNER_AI) {
        runtime->state = XIAOTAI_STATE_AI_CONNECTING;
    } else if (owner == XIAOTAI_OWNER_ROOM) {
        runtime->state = XIAOTAI_STATE_ROOM_CONNECTING;
    } else {
        runtime->state = incoming ? XIAOTAI_STATE_CALL_INCOMING
                                  : XIAOTAI_STATE_CALL_CONNECTING;
    }
    return true;
}

bool xiaotai_runtime_connected(xiaotai_runtime_t *runtime,
                               uint32_t generation)
{
    if (runtime == NULL || generation != runtime->generation ||
        runtime->owner == XIAOTAI_OWNER_NONE) {
        return false;
    }
    if (runtime->state == XIAOTAI_STATE_CALL_INCOMING) {
        runtime->state = XIAOTAI_STATE_CALL_CONNECTING;
    }
    return true;
}

bool xiaotai_runtime_media_started(xiaotai_runtime_t *runtime,
                                   uint32_t generation)
{
    if (runtime == NULL || generation != runtime->generation) return false;
    if (runtime->state == XIAOTAI_STATE_AI_CONNECTING) {
        runtime->state = XIAOTAI_STATE_AI_ACTIVE;
        runtime->deadline_ms = 0;
        return true;
    }
    if (runtime->state == XIAOTAI_STATE_CALL_CONNECTING ||
        runtime->state == XIAOTAI_STATE_CALL_INCOMING) {
        runtime->state = XIAOTAI_STATE_CALL_ACTIVE;
        runtime->deadline_ms = 0;
        return true;
    }
    if (runtime->state == XIAOTAI_STATE_ROOM_CONNECTING) {
        runtime->state = XIAOTAI_STATE_ROOM_ACTIVE;
        runtime->deadline_ms = 0;
        return true;
    }
    return runtime->state == XIAOTAI_STATE_STREAM_ACTIVE;
}

bool xiaotai_runtime_begin_ending(xiaotai_runtime_t *runtime,
                                  uint32_t generation,
                                  uint32_t now_ms,
                                  uint32_t timeout_ms)
{
    if (runtime == NULL || generation != runtime->generation ||
        timeout_ms == 0U ||
        (runtime->owner != XIAOTAI_OWNER_DEVICE_CALL &&
         runtime->owner != XIAOTAI_OWNER_WECHAT_VOIP) ||
        (runtime->state != XIAOTAI_STATE_CALL_CONNECTING &&
         runtime->state != XIAOTAI_STATE_CALL_ACTIVE)) {
        return false;
    }
    runtime->state = XIAOTAI_STATE_CALL_ENDING;
    runtime->deadline_ms = now_ms + timeout_ms;
    if (runtime->deadline_ms == 0U) runtime->deadline_ms = 1U;
    return true;
}

bool xiaotai_runtime_arm_timeout(xiaotai_runtime_t *runtime,
                                 uint32_t generation,
                                 uint32_t now_ms,
                                 uint32_t timeout_ms)
{
    if (runtime == NULL || runtime->owner == XIAOTAI_OWNER_NONE ||
        runtime->generation != generation || timeout_ms == 0U) {
        return false;
    }
    runtime->deadline_ms = now_ms + timeout_ms;
    if (runtime->deadline_ms == 0U) runtime->deadline_ms = 1U;
    return true;
}

bool xiaotai_runtime_expire(xiaotai_runtime_t *runtime,
                            uint32_t now_ms,
                            xiaotai_session_owner_t *owner,
                            xiaotai_runtime_state_t *state,
                            uint32_t *generation)
{
    if (runtime == NULL || runtime->owner == XIAOTAI_OWNER_NONE ||
        runtime->deadline_ms == 0U ||
        (int32_t)(now_ms - runtime->deadline_ms) < 0) {
        return false;
    }
    if (owner != NULL) *owner = runtime->owner;
    if (state != NULL) *state = runtime->state;
    if (generation != NULL) *generation = runtime->generation;
    runtime->owner = XIAOTAI_OWNER_NONE;
    runtime->state = XIAOTAI_STATE_WAITING;
    runtime->deadline_ms = 0;
    return true;
}

bool xiaotai_runtime_finish(xiaotai_runtime_t *runtime,
                            uint32_t generation)
{
    if (runtime == NULL || generation != runtime->generation ||
        runtime->owner == XIAOTAI_OWNER_NONE) {
        return false;
    }
    runtime->owner = XIAOTAI_OWNER_NONE;
    runtime->state = XIAOTAI_STATE_WAITING;
    runtime->deadline_ms = 0;
    return true;
}

static bool incoming_owner(xiaotai_session_owner_t owner)
{
    return owner == XIAOTAI_OWNER_DEVICE_CALL ||
           owner == XIAOTAI_OWNER_WECHAT_VOIP;
}

bool xiaotai_runtime_offer_incoming(xiaotai_runtime_t *runtime,
                                    xiaotai_session_owner_t owner,
                                    uint32_t now_ms,
                                    uint32_t timeout_ms)
{
    if (runtime == NULL || !incoming_owner(owner) || timeout_ms == 0U ||
        runtime->pending_incoming != XIAOTAI_OWNER_NONE ||
        incoming_owner(runtime->owner)) {
        return false;
    }
    runtime->pending_generation++;
    if (runtime->pending_generation == 0U) runtime->pending_generation = 1U;
    runtime->pending_incoming = owner;
    runtime->pending_deadline_ms = now_ms + timeout_ms;
    if (runtime->pending_deadline_ms == 0U) runtime->pending_deadline_ms = 1U;
    return true;
}

bool xiaotai_runtime_has_incoming(const xiaotai_runtime_t *runtime)
{
    return runtime != NULL &&
           runtime->pending_incoming != XIAOTAI_OWNER_NONE;
}

bool xiaotai_runtime_home_allowed(const xiaotai_runtime_t *runtime)
{
    return runtime != NULL && runtime->owner == XIAOTAI_OWNER_NONE &&
           !xiaotai_runtime_has_incoming(runtime);
}

bool xiaotai_runtime_accept_incoming(xiaotai_runtime_t *runtime,
                                     xiaotai_session_owner_t *owner,
                                     uint32_t *generation)
{
    if (runtime == NULL || runtime->owner != XIAOTAI_OWNER_NONE ||
        !incoming_owner(runtime->pending_incoming)) {
        return false;
    }
    xiaotai_session_owner_t accepted = runtime->pending_incoming;
    runtime->pending_incoming = XIAOTAI_OWNER_NONE;
    runtime->pending_deadline_ms = 0U;
    /* The notification already lived in pending_incoming. Once the user
     * accepts it, the foreground session starts in the connecting phase. */
    if (!xiaotai_runtime_begin(runtime, accepted, false)) return false;
    if (owner != NULL) *owner = accepted;
    if (generation != NULL) *generation = runtime->generation;
    return true;
}

bool xiaotai_runtime_cancel_incoming(xiaotai_runtime_t *runtime,
                                     xiaotai_session_owner_t *owner)
{
    if (runtime == NULL || !incoming_owner(runtime->pending_incoming)) {
        return false;
    }
    if (owner != NULL) *owner = runtime->pending_incoming;
    runtime->pending_incoming = XIAOTAI_OWNER_NONE;
    runtime->pending_deadline_ms = 0U;
    return true;
}

bool xiaotai_runtime_expire_incoming(xiaotai_runtime_t *runtime,
                                     uint32_t now_ms,
                                     xiaotai_session_owner_t *owner)
{
    if (runtime == NULL || !incoming_owner(runtime->pending_incoming) ||
        runtime->pending_deadline_ms == 0U ||
        (int32_t)(now_ms - runtime->pending_deadline_ms) < 0) {
        return false;
    }
    return xiaotai_runtime_cancel_incoming(runtime, owner);
}

bool xiaotai_runtime_remote_view_allowed(const xiaotai_runtime_t *runtime,
                                         bool room_active)
{
    return runtime != NULL && runtime->owner == XIAOTAI_OWNER_NONE &&
           !room_active;
}
