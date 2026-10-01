#include "xiaotai_call_state.h"

#include <assert.h>
#include <stdint.h>
#include <string.h>

static void invalid_device_redial_preserves_current_call(void)
{
    xiaotai_call_state_t state;
    xiaotai_call_state_init(&state);

    assert(xiaotai_call_state_begin_device_outbound(&state, "device-1"));
    assert(xiaotai_call_state_set_device_room(&state, "room-1"));

    assert(!xiaotai_call_state_begin_device_outbound(&state, ""));
    assert(state.device.outbound);
    assert(strcmp(state.device.peer_id, "device-1") == 0);
    assert(strcmp(state.device.room_id, "room-1") == 0);
    assert(strcmp(state.device.type, "audio") == 0);
}

static void invalid_voip_redial_preserves_current_call(void)
{
    xiaotai_call_state_t state;
    xiaotai_call_state_init(&state);

    assert(xiaotai_call_state_begin_voip_outbound(&state, "openid-1"));
    assert(xiaotai_call_state_set_voip_call_id(&state, "call-1"));
    assert(xiaotai_call_state_accept_voip(&state, "whips://peer", "token",
                                          "wx-room", "call-1", false));

    assert(!xiaotai_call_state_begin_voip_outbound(&state, ""));
    assert(state.voip.outbound);
    assert(strcmp(state.voip.outbound_openid, "openid-1") == 0);
    assert(strcmp(state.voip.call_id, "call-1") == 0);
    assert(strcmp(state.voip.room_id, "wx-room") == 0);
    assert(strcmp(state.voip.peer_id, "whips://peer") == 0);
    assert(strcmp(state.voip.token, "token") == 0);
}

static void invalid_incoming_offer_preserves_current_call(void)
{
    xiaotai_call_state_t state;
    xiaotai_call_state_init(&state);
    assert(xiaotai_call_state_set_device_incoming(
        &state, "room-1", "caller-1", "video"));

    assert(!xiaotai_call_state_set_device_incoming(
        &state, "", "caller-2", "audio"));
    assert(strcmp(state.device.room_id, "room-1") == 0);
    assert(strcmp(state.device.peer_id, "caller-1") == 0);
    assert(strcmp(state.device.type, "video") == 0);
}

static void invalid_voip_accept_preserves_current_call(void)
{
    xiaotai_call_state_t state;
    xiaotai_call_state_init(&state);
    assert(xiaotai_call_state_accept_voip(&state, "peer-1", "token-1",
                                          "room-1", "call-1", false));

    assert(!xiaotai_call_state_accept_voip(&state, "", "token-2",
                                           "room-2", "call-2", true));
    assert(strcmp(state.voip.peer_id, "peer-1") == 0);
    assert(strcmp(state.voip.token, "token-1") == 0);
    assert(strcmp(state.voip.room_id, "room-1") == 0);
    assert(strcmp(state.voip.call_id, "call-1") == 0);
}

static void stale_offer_deadline_handles_clock_wrap(void)
{
    xiaotai_call_state_t state;
    xiaotai_call_state_init(&state);
    xiaotai_call_state_mark_voip_stale(&state, "call-old", 10U);

    assert(xiaotai_call_state_classify_voip(
        &state, false, UINT32_MAX - 5U, "self", "peer", "token", "room",
        "call-old", "", "other") == XIAOTAI_VOIP_OFFER_STALE);
    assert(xiaotai_call_state_classify_voip(
        &state, false, 10U, "self", "peer", "token", "room", "call-old",
        "", "other") == XIAOTAI_VOIP_OFFER_INCOMING);
}

static void outbound_offer_without_call_id_matches_only_self_notification(void)
{
    xiaotai_call_state_t state;
    xiaotai_call_state_init(&state);
    assert(xiaotai_call_state_begin_voip_outbound(&state, "openid-1"));

    assert(xiaotai_call_state_classify_voip(
        &state, true, 100U, "self", "peer", "token", "room", "",
        "openid-1", "self") == XIAOTAI_VOIP_OFFER_OUTBOUND_MATCH);
    assert(xiaotai_call_state_classify_voip(
        &state, true, 100U, "self", "peer", "token", "room", "call-2",
        "openid-1", "other") == XIAOTAI_VOIP_OFFER_OUTBOUND_MISMATCH);
    assert(xiaotai_call_state_classify_voip(
        &state, true, 100U, "self", "peer", "token", "room", "",
        "openid-2", "self") == XIAOTAI_VOIP_OFFER_OUTBOUND_MISMATCH);
}

static void oversized_offer_is_rejected(void)
{
    xiaotai_call_state_t state;
    char oversized_peer[XIAOTAI_VOIP_DESCRIPTOR_SIZE + 1U];
    xiaotai_call_state_init(&state);
    memset(oversized_peer, 'p', sizeof(oversized_peer) - 1U);
    oversized_peer[sizeof(oversized_peer) - 1U] = '\0';

    assert(xiaotai_call_state_classify_voip(
        &state, false, 100U, "self", oversized_peer, "token", "room",
        "call", "openid", "other") == XIAOTAI_VOIP_OFFER_INVALID);
}

int main(void)
{
    invalid_device_redial_preserves_current_call();
    invalid_voip_redial_preserves_current_call();
    invalid_incoming_offer_preserves_current_call();
    invalid_voip_accept_preserves_current_call();
    stale_offer_deadline_handles_clock_wrap();
    outbound_offer_without_call_id_matches_only_self_notification();
    oversized_offer_is_rejected();
    return 0;
}
