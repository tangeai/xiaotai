#include "xiaotai_call_state.h"

#include <string.h>

static bool copy_value(char *output, size_t capacity, const char *value,
                       bool allow_empty)
{
    if (output == NULL || capacity == 0U || value == NULL) return false;
    size_t length = strlen(value);
    if ((!allow_empty && length == 0U) || length >= capacity) return false;
    memcpy(output, value, length + 1U);
    return true;
}

void xiaotai_call_state_init(xiaotai_call_state_t *state)
{
    if (state != NULL) memset(state, 0, sizeof(*state));
}

void xiaotai_call_state_clear_device(xiaotai_call_state_t *state)
{
    if (state != NULL) memset(&state->device, 0, sizeof(state->device));
}

void xiaotai_call_state_clear_voip(xiaotai_call_state_t *state)
{
    if (state != NULL) memset(&state->voip, 0, sizeof(state->voip));
}

bool xiaotai_call_state_begin_device_outbound(xiaotai_call_state_t *state,
                                              const char *peer_id)
{
    if (state == NULL) return false;
    xiaotai_device_call_state_t next = {0};
    if (!copy_value(next.peer_id, sizeof(next.peer_id), peer_id, false)) {
        return false;
    }
    memcpy(next.type, "audio", sizeof("audio"));
    next.outbound = true;
    state->device = next;
    return true;
}

bool xiaotai_call_state_begin_voip_outbound(xiaotai_call_state_t *state,
                                            const char *openid)
{
    if (state == NULL) return false;
    xiaotai_voip_call_state_t next = {0};
    if (!copy_value(next.outbound_openid,
                    sizeof(next.outbound_openid), openid, false)) {
        return false;
    }
    next.outbound = true;
    state->voip = next;
    return true;
}

bool xiaotai_call_state_set_device_incoming(xiaotai_call_state_t *state,
                                            const char *room_id,
                                            const char *caller_id,
                                            const char *call_type)
{
    if (state == NULL) return false;
    xiaotai_device_call_state_t next = {0};
    if (!copy_value(next.room_id, sizeof(next.room_id), room_id, false) ||
        !copy_value(next.peer_id, sizeof(next.peer_id), caller_id, false)) {
        return false;
    }
    const char *normalized = call_type != NULL &&
        strcmp(call_type, "video") == 0 ? "video" : "audio";
    (void)copy_value(next.type, sizeof(next.type), normalized, false);
    state->device = next;
    return true;
}

bool xiaotai_call_state_set_device_room(xiaotai_call_state_t *state,
                                        const char *room_id)
{
    return state != NULL && copy_value(state->device.room_id,
                                       sizeof(state->device.room_id),
                                       room_id, false);
}

bool xiaotai_call_state_set_voip_call_id(xiaotai_call_state_t *state,
                                         const char *call_id)
{
    return state != NULL && copy_value(state->voip.call_id,
                                       sizeof(state->voip.call_id),
                                       call_id, false);
}

void xiaotai_call_state_mark_voip_stale(xiaotai_call_state_t *state,
                                        const char *call_id,
                                        uint32_t deadline_ms)
{
    if (state == NULL ||
        !copy_value(state->stale_voip_call_id,
                    sizeof(state->stale_voip_call_id), call_id, false)) return;
    state->stale_voip_deadline_ms = deadline_ms;
}

xiaotai_voip_offer_t xiaotai_call_state_classify_voip(
    const xiaotai_call_state_t *state, bool outbound_pending,
    uint32_t now_ms, const char *device_id, const char *peer_id,
    const char *token, const char *room_id, const char *call_id,
    const char *openid, const char *from)
{
    if (state == NULL || device_id == NULL || peer_id == NULL ||
        token == NULL || room_id == NULL || call_id == NULL ||
        openid == NULL || from == NULL || peer_id[0] == '\0' ||
        token[0] == '\0' || room_id[0] == '\0' ||
        strlen(peer_id) >= sizeof(state->voip.peer_id) ||
        strlen(token) >= sizeof(state->voip.token) ||
        strlen(room_id) >= sizeof(state->voip.room_id) ||
        strlen(call_id) >= sizeof(state->voip.call_id) ||
        strlen(openid) >= sizeof(state->voip.outbound_openid)) {
        return XIAOTAI_VOIP_OFFER_INVALID;
    }
    if (call_id[0] != '\0' &&
        strcmp(call_id, state->stale_voip_call_id) == 0 &&
        (int32_t)(now_ms - state->stale_voip_deadline_ms) < 0) {
        return XIAOTAI_VOIP_OFFER_STALE;
    }
    if (!outbound_pending || !state->voip.outbound) {
        return XIAOTAI_VOIP_OFFER_INCOMING;
    }
    bool openid_matches = openid[0] == '\0' ||
        strcmp(openid, state->voip.outbound_openid) == 0;
    bool call_matches = state->voip.call_id[0] != '\0' ?
        (call_id[0] != '\0' && strcmp(call_id, state->voip.call_id) == 0) :
        (call_id[0] == '\0' || strcmp(from, device_id) == 0);
    return openid_matches && call_matches ?
        XIAOTAI_VOIP_OFFER_OUTBOUND_MATCH :
        XIAOTAI_VOIP_OFFER_OUTBOUND_MISMATCH;
}

bool xiaotai_call_state_accept_voip(xiaotai_call_state_t *state,
                                    const char *peer_id, const char *token,
                                    const char *room_id, const char *call_id,
                                    bool complete_outbound)
{
    if (state == NULL) return false;
    xiaotai_voip_call_state_t next = state->voip;
    if (!copy_value(next.peer_id, sizeof(next.peer_id), peer_id, false) ||
        !copy_value(next.token, sizeof(next.token), token, false) ||
        !copy_value(next.room_id, sizeof(next.room_id), room_id, false) ||
        !copy_value(next.call_id, sizeof(next.call_id), call_id, true)) {
        return false;
    }
    if (complete_outbound) next.outbound = false;
    state->voip = next;
    return true;
}

bool xiaotai_call_state_matches_device(const xiaotai_call_state_t *state,
                                       const char *room_id)
{
    return state != NULL && room_id != NULL &&
           (room_id[0] == '\0' ||
            strcmp(room_id, state->device.room_id) == 0);
}

bool xiaotai_call_state_matches_voip(const xiaotai_call_state_t *state,
                                     const char *room_id)
{
    return state != NULL && room_id != NULL &&
           (room_id[0] == '\0' || strcmp(room_id, state->voip.room_id) == 0);
}
