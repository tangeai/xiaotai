#include "xiaotai_call_state.h"

#include <assert.h>
#include <string.h>

int main(void)
{
    xiaotai_call_state_t state;
    xiaotai_call_state_init(&state);
    assert(xiaotai_call_state_begin_device_outbound(&state, "dev-2"));
    assert(state.device.outbound);
    assert(strcmp(state.device.type, "audio") == 0);
    assert(xiaotai_call_state_set_device_room(&state, "room-1"));
    assert(xiaotai_call_state_matches_device(&state, "room-1"));
    xiaotai_call_state_clear_device(&state);
    assert(state.device.room_id[0] == '\0');

    assert(xiaotai_call_state_begin_voip_outbound(&state, "openid-1"));
    assert(xiaotai_call_state_set_voip_call_id(&state, "call-1"));
    assert(xiaotai_call_state_classify_voip(
        &state, true, 100, "self", "whips://peer", "token", "wx-room",
        "call-1", "openid-1", "other") ==
        XIAOTAI_VOIP_OFFER_OUTBOUND_MATCH);
    assert(xiaotai_call_state_classify_voip(
        &state, true, 100, "self", "whips://peer", "token", "wx-room",
        "call-2", "openid-1", "other") ==
        XIAOTAI_VOIP_OFFER_OUTBOUND_MISMATCH);
    assert(xiaotai_call_state_accept_voip(&state, "whips://peer", "token",
                                          "wx-room", "call-1", true));
    assert(!state.voip.outbound);
    assert(xiaotai_call_state_matches_voip(&state, "wx-room"));

    xiaotai_call_state_mark_voip_stale(&state, "late", 200);
    assert(xiaotai_call_state_classify_voip(
        &state, false, 150, "self", "peer", "token", "room", "late", "",
        "other") == XIAOTAI_VOIP_OFFER_STALE);
    assert(xiaotai_call_state_classify_voip(
        &state, false, 201, "self", "peer", "token", "room", "late", "",
        "other") == XIAOTAI_VOIP_OFFER_INCOMING);
    return 0;
}
