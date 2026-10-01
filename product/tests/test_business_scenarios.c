#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "xiaotai_ai_protocol.h"
#include "xiaotai_call_state.h"
#include "xiaotai_contacts.h"
#include "xiaotai_media_contract.h"
#include "xiaotai_runtime.h"
#include "xiaotai_signal.h"

typedef struct {
    unsigned visits;
    xiaotai_signal_type_t type;
    char room_id[XIAOTAI_CALL_ROOM_ID_SIZE];
    char call_id[XIAOTAI_CALL_ROOM_ID_SIZE];
} signal_capture_t;

static void capture_signal(const xiaotai_signal_view_t *signal, void *context)
{
    signal_capture_t *capture = context;
    ++capture->visits;
    capture->type = signal->type;
    const char *room_id = signal->wx_room_id != NULL &&
                          signal->wx_room_id[0] != '\0' ?
                          signal->wx_room_id : signal->room_id;
    if (room_id != NULL) {
        snprintf(capture->room_id, sizeof(capture->room_id), "%s",
                 room_id);
    }
    if (signal->wx_call_id != NULL) {
        snprintf(capture->call_id, sizeof(capture->call_id), "%s",
                 signal->wx_call_id);
    }
}

static void remote_view_then_incoming_voip(void)
{
    xiaotai_runtime_t runtime;
    xiaotai_call_state_t call;
    xiaotai_media_contract_t media;
    xiaotai_runtime_init(&runtime);
    xiaotai_call_state_init(&call);

    assert(xiaotai_runtime_begin(&runtime, XIAOTAI_OWNER_STREAM, true));
    uint32_t stream_generation = runtime.generation;
    assert(xiaotai_runtime_connected(&runtime, stream_generation));
    assert(xiaotai_runtime_media_started(&runtime, stream_generation));
    assert(runtime.state == XIAOTAI_STATE_STREAM_ACTIVE);
    assert(xiaotai_media_contract_get(XIAOTAI_MEDIA_MODE_STREAM, &media));
    assert(media.up_audio_stream_id == 10U &&
           media.down_audio_stream_id == 14U);

    signal_capture_t signal = {0};
    const char notification[] =
        "{\"type\":\"call_incoming\",\"channel\":\"voip\",\"payload\":{"
        "\"peer_id\":\"whips://peer\",\"token\":\"secret\","
        "\"wx_room_id\":\"wx-room\",\"wx_call_id\":\"call-7\","
        "\"wx_user_openid\":\"wx-user\",\"wx_from\":\"wx-user\"}}";
    assert(xiaotai_signal_decode(notification, capture_signal, &signal));
    assert(signal.visits == 1U &&
           signal.type == XIAOTAI_SIGNAL_VOIP_CALL_INCOMING);
    assert(strcmp(signal.room_id, "wx-room") == 0);
    assert(strcmp(signal.call_id, "call-7") == 0);
    assert(xiaotai_runtime_offer_incoming(&runtime,
        XIAOTAI_OWNER_WECHAT_VOIP, 100U, 45000U));

    assert(!xiaotai_runtime_accept_incoming(&runtime, NULL, NULL));
    assert(xiaotai_runtime_finish(&runtime, stream_generation));

    xiaotai_session_owner_t owner = XIAOTAI_OWNER_NONE;
    uint32_t voip_generation = 0U;
    assert(xiaotai_runtime_accept_incoming(&runtime, &owner,
                                            &voip_generation));
    assert(owner == XIAOTAI_OWNER_WECHAT_VOIP);
    assert(xiaotai_call_state_accept_voip(&call, "whips://peer", "secret",
                                           "wx-room", "call-7", false));
    assert(xiaotai_runtime_connected(&runtime, voip_generation));
    assert(xiaotai_runtime_media_started(&runtime, voip_generation));
    assert(xiaotai_media_contract_get(XIAOTAI_MEDIA_MODE_WECHAT_VOIP,
                                       &media));
    assert(media.up_audio_stream_id == 0U &&
           media.down_audio_stream_id == 0U);
    assert(!xiaotai_runtime_media_started(&runtime, stream_generation));
    assert(xiaotai_runtime_finish(&runtime, voip_generation));
    xiaotai_call_state_clear_voip(&call);
}

static void incoming_notification_preserves_active_session(void)
{
    const xiaotai_session_owner_t active_owners[] = {
        XIAOTAI_OWNER_STREAM,
        XIAOTAI_OWNER_AI,
        XIAOTAI_OWNER_ROOM,
    };

    for (size_t i = 0; i < sizeof(active_owners) / sizeof(active_owners[0]);
         ++i) {
        xiaotai_runtime_t runtime;
        xiaotai_runtime_init(&runtime);
        assert(xiaotai_runtime_begin(&runtime, active_owners[i], false));
        uint32_t active_generation = runtime.generation;
        assert(xiaotai_runtime_media_started(&runtime, active_generation));

        assert(xiaotai_runtime_offer_incoming(&runtime,
            XIAOTAI_OWNER_DEVICE_CALL, 100U, 45000U));
        assert(runtime.owner == active_owners[i]);
        assert(runtime.generation == active_generation);
        assert(xiaotai_runtime_has_incoming(&runtime));
        assert(!xiaotai_runtime_accept_incoming(&runtime, NULL, NULL));
        assert(!xiaotai_runtime_offer_incoming(&runtime,
            XIAOTAI_OWNER_WECHAT_VOIP, 101U, 45000U));

        xiaotai_session_owner_t rejected = XIAOTAI_OWNER_NONE;
        assert(xiaotai_runtime_cancel_incoming(&runtime, &rejected));
        assert(rejected == XIAOTAI_OWNER_DEVICE_CALL);
        assert(runtime.owner == active_owners[i]);
        assert(runtime.generation == active_generation);
        assert(!xiaotai_runtime_has_incoming(&runtime));
        assert(xiaotai_runtime_finish(&runtime, active_generation));
    }
}

static void incoming_timeout_preserves_active_session(void)
{
    xiaotai_runtime_t runtime;
    xiaotai_runtime_init(&runtime);

    assert(xiaotai_runtime_begin(&runtime, XIAOTAI_OWNER_AI, false));
    uint32_t ai_generation = runtime.generation;
    assert(xiaotai_runtime_media_started(&runtime, ai_generation));
    assert(xiaotai_runtime_offer_incoming(&runtime,
        XIAOTAI_OWNER_WECHAT_VOIP, 100U, 500U));
    assert(!xiaotai_runtime_expire_incoming(&runtime, 599U, NULL));

    xiaotai_session_owner_t expired = XIAOTAI_OWNER_NONE;
    assert(xiaotai_runtime_expire_incoming(&runtime, 600U, &expired));
    assert(expired == XIAOTAI_OWNER_WECHAT_VOIP);
    assert(runtime.owner == XIAOTAI_OWNER_AI);
    assert(runtime.generation == ai_generation);
    assert(runtime.state == XIAOTAI_STATE_AI_ACTIVE);
    assert(xiaotai_runtime_finish(&runtime, ai_generation));
}

static void explicit_answer_switches_from_room_to_call(void)
{
    xiaotai_runtime_t runtime;
    xiaotai_runtime_init(&runtime);

    assert(xiaotai_runtime_begin(&runtime, XIAOTAI_OWNER_ROOM, false));
    uint32_t room_generation = runtime.generation;
    assert(xiaotai_runtime_media_started(&runtime, room_generation));
    assert(xiaotai_runtime_offer_incoming(&runtime,
        XIAOTAI_OWNER_DEVICE_CALL, 100U, 45000U));

    /* User acceptance is a two-step handoff: release current media first. */
    assert(!xiaotai_runtime_accept_incoming(&runtime, NULL, NULL));
    assert(xiaotai_runtime_finish(&runtime, room_generation));

    xiaotai_session_owner_t accepted = XIAOTAI_OWNER_NONE;
    uint32_t call_generation = 0U;
    assert(xiaotai_runtime_accept_incoming(&runtime, &accepted,
                                            &call_generation));
    assert(accepted == XIAOTAI_OWNER_DEVICE_CALL);
    assert(call_generation != room_generation);
    assert(runtime.state == XIAOTAI_STATE_CALL_CONNECTING);
    assert(xiaotai_runtime_connected(&runtime, call_generation));
    assert(xiaotai_runtime_media_started(&runtime, call_generation));
    assert(runtime.state == XIAOTAI_STATE_CALL_ACTIVE);
    assert(!xiaotai_runtime_finish(&runtime, room_generation));
    assert(xiaotai_runtime_finish(&runtime, call_generation));
}

static void remote_view_requires_idle_media(void)
{
    const xiaotai_session_owner_t owners[] = {
        XIAOTAI_OWNER_AI,
        XIAOTAI_OWNER_DEVICE_CALL,
        XIAOTAI_OWNER_WECHAT_VOIP,
        XIAOTAI_OWNER_ROOM,
    };
    xiaotai_runtime_t runtime;
    xiaotai_runtime_init(&runtime);

    assert(xiaotai_runtime_remote_view_allowed(&runtime, false));
    assert(!xiaotai_runtime_remote_view_allowed(&runtime, true));
    for (size_t i = 0; i < sizeof(owners) / sizeof(owners[0]); ++i) {
        assert(xiaotai_runtime_begin(&runtime, owners[i], false));
        assert(!xiaotai_runtime_remote_view_allowed(&runtime, false));
        assert(xiaotai_runtime_finish(&runtime, runtime.generation));
    }
}

static void ai_call_intent_handoff(void)
{
    xiaotai_runtime_t runtime;
    xiaotai_contacts_t contacts;
    xiaotai_contact_t target;
    xiaotai_media_contract_t media;
    char request[384];
    xiaotai_runtime_init(&runtime);
    xiaotai_contacts_init(&contacts);

    assert(xiaotai_contacts_replace_voip_json(&contacts,
        "{\"code\":0,\"data\":{\"contacts\":[{"
        "\"wx_open_id\":\"wx-2\",\"wxa_model_id\":\"model-2\","
        "\"wx_app_id\":\"app-2\",\"alias\":\"爸爸\"}]}}") == 1);
    assert(xiaotai_runtime_begin(&runtime, XIAOTAI_OWNER_AI, false));
    uint32_t ai_generation = runtime.generation;
    assert(xiaotai_ai_encode_start(request, sizeof(request), "req-2",
                                    "dev-1", "role-1") > 0);
    const char accepted[] =
        "{\"id\":\"req-2\",\"result\":{\"session_id\":\"s\","
        "\"input_audio\":{\"codec\":\"opus\",\"sample_rate\":16000,"
        "\"channels\":1},\"output_audio\":{\"codec\":\"opus\","
        "\"sample_rate\":16000,\"channels\":1}}}";
    assert(xiaotai_ai_decode_message(accepted, strlen(accepted), "req-2") ==
           XIAOTAI_AI_MESSAGE_START_ACCEPTED);
    assert(xiaotai_runtime_media_started(&runtime, ai_generation));
    assert(xiaotai_media_contract_get(XIAOTAI_MEDIA_MODE_AI, &media));
    assert(media.codec == XIAOTAI_MEDIA_CODEC_OPUS &&
           media.sample_rate_hz == 16000U);
    assert(xiaotai_contacts_resolve_call_intent_json(&contacts,
        "{\"method\":\"call_intent\",\"params\":{"
        "\"target_name\":\"爸爸\",\"channel\":\"wechat\","
        "\"media\":\"audio\"}}", &target) == XIAOTAI_CONTACT_MATCH_OK);
    assert(target.type == XIAOTAI_CONTACT_VOIP);

    assert(xiaotai_runtime_finish(&runtime, ai_generation));
    assert(xiaotai_runtime_begin(&runtime, XIAOTAI_OWNER_WECHAT_VOIP, false));
    assert(!xiaotai_runtime_finish(&runtime, ai_generation));
    assert(xiaotai_runtime_finish(&runtime, runtime.generation));
}

int main(void)
{
    remote_view_then_incoming_voip();
    incoming_notification_preserves_active_session();
    incoming_timeout_preserves_active_session();
    explicit_answer_switches_from_room_to_call();
    remote_view_requires_idle_media();
    ai_call_intent_handoff();
    return 0;
}
