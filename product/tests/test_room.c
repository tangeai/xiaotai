#include <assert.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "xiaotai_room.h"

static uint32_t clock_ms;
static bool connected;
static bool uplink = true;
static unsigned assignment_requests;
static unsigned create_requests;
static unsigned diagnostic_events;
static xiaotai_room_diagnostic_t last_diagnostic;
static xiaotai_room_t *active_room;

static void post_response(xiaotai_room_response_t type, uint32_t generation,
                          const char *text, size_t length, void *context)
{
    (void)length;
    (void)context;
    xiaotai_room_handle_response(active_room, type, generation, text);
}

static uint32_t now_ms(void *context) { (void)context; return clock_ms; }
static bool random_bytes(void *buffer, size_t size, void *context)
{
    (void)context;
    memset(buffer, 0x5a, size);
    return true;
}
static bool ready(void *context) { (void)context; return true; }
static bool busy(void *context) { (void)context; return false; }
static bool is_connected(void *context) { (void)context; return connected; }
static int connect_transport(const char *peer, const char *token,
                             uint32_t generation, void *context)
{
    (void)context;
    assert(strcmp(peer, "peer") == 0 && strcmp(token, "token") == 0);
    assert(generation != 0U);
    connected = true;
    return 0;
}
static int start_media(void *context) { (void)context; return 0; }
static int send_command(uint32_t command, const void *data, uint32_t length,
                        void *context)
{
    (void)context;
    assert(command == XIAOTAI_ROOM_COMMAND && data != NULL && length != 0U);
    return 0;
}
static int disconnect_transport(void *context)
{
    (void)context;
    connected = false;
    return 0;
}
static void set_uplink(bool enabled, void *context)
{
    (void)context;
    uplink = enabled;
}
static int request(const char *path, const char *body,
                   xiaotai_room_service_response_fn callback,
                   void *callback_context, void *context)
{
    (void)context;
    if (strstr(path, "/create") != NULL) {
        assert(strcmp(body, "{\"password\":\"\"}") == 0);
        ++create_requests;
        callback("{\"code\":0,\"data\":{\"desired_state\":\"joined\","
                 "\"assignment_version\":1,\"room_id\":\"room-new\","
                 "\"room_code\":\"654321\"}}", callback_context);
    } else if (strstr(path, "assignment") != NULL) {
        ++assignment_requests;
        callback("{\"code\":0,\"data\":{\"desired_state\":\"joined\","
                 "\"assignment_version\":1,\"room_id\":\"room-1\","
                 "\"room_code\":\"123456\"}}", callback_context);
    } else if (strstr(path, "connect-token") != NULL) {
        callback("{\"code\":0,\"data\":{\"peer_id\":\"peer\","
                 "\"token\":\"token\",\"heartbeat_seconds\":15,"
                 "\"lease_seconds\":45}}", callback_context);
    } else if (strstr(path, "presence") != NULL) {
        callback("{\"code\":0,\"data\":{}}", callback_context);
    }
    return 0;
}
static bool acquire(uint32_t *generation, void *context)
{
    (void)context;
    *generation = 9U;
    return true;
}
static bool activate(uint32_t generation, void *context)
{
    (void)context;
    return generation == 9U;
}
static void release(uint32_t generation, void *context)
{
    (void)context;
    assert(generation == 9U);
}
static bool available(void *context) { (void)context; return true; }
static void on_diagnostic(xiaotai_room_diagnostic_t event,
                          uint32_t generation, int detail, void *context)
{
    (void)generation;
    (void)detail;
    (void)context;
    diagnostic_events++;
    last_diagnostic = event;
}

int main(void)
{
    xiaotai_room_port_t port = {
        .service_request = request, .now_ms = now_ms,
        .random_bytes = random_bytes, .service_ready = ready,
        .transport_busy = busy, .transport_connected = is_connected,
        .transport_connect = connect_transport,
        .transport_start_media = start_media, .transport_send = send_command,
        .transport_disconnect = disconnect_transport,
        .set_uplink_enabled = set_uplink, .media_acquire = acquire,
        .media_activate = activate, .media_release = release,
        .media_available = available, .post_response = post_response,
        .diagnostic = on_diagnostic,
    };
    xiaotai_room_t room;
    active_room = &room;
    xiaotai_room_init(&room, "device-1", &port);
    assert(xiaotai_room_action(&room, XIAOTAI_ROOM_ACTION_CREATE,
                               "{\"password\":\"\"}") == 0);
    assert(create_requests == 1U);
    assert(room.assigned && room.connecting);

    connected = false;
    xiaotai_room_init(&room, "device-1", &port);
    xiaotai_room_tick(&room);
    assert(assignment_requests == 0U);
    assert(xiaotai_room_action(&room, XIAOTAI_ROOM_ACTION_SYNC, NULL) == 0);
    xiaotai_room_tick(&room);
    assert(assignment_requests == 1U && room.connecting && connected);

    xiaotai_room_handle_connected(&room, room.generation);
    xiaotai_room_handle_command(&room, room.generation,
        "{\"id\":1,\"result\":{\"session_id\":\"session\","
        "\"input_audio\":{\"codec\":\"g711a\",\"sample_rate\":8000,"
        "\"channels\":1},\"output_audio\":{\"codec\":\"g711a\","
        "\"sample_rate\":8000,\"channels\":1}}}");
    assert(room.joined && !room.connecting && !uplink);
    xiaotai_room_handle_command(&room, room.generation,
        "{\"method\":\"room_snapshot\",\"params\":{\"participants\":["
        "{\"device_id\":\"device-1\",\"device_name\":\"厨房\","
        "\"mic_state\":\"speaking\"},"
        "{\"participant_id\":\"guest-2\",\"display_name\":\"客厅\","
        "\"mic_state\":\"off\"}]}}" );
    xiaotai_room_snapshot_t snapshot;
    xiaotai_room_snapshot(&room, &snapshot);
    assert(snapshot.members == 2U);
    assert(snapshot.participant_count == 2U);
    assert(strcmp(snapshot.participants[0].id, "device-1") == 0);
    assert(strcmp(snapshot.participants[0].name, "厨房") == 0);
    assert(snapshot.participants[0].self);
    assert(snapshot.participants[0].speaking);
    assert(strcmp(snapshot.participants[1].id, "guest-2") == 0);
    assert(strcmp(snapshot.participants[1].name, "客厅") == 0);
    assert(!snapshot.participants[1].self);
    assert(!snapshot.participants[1].speaking);
    assert(xiaotai_room_action(&room, XIAOTAI_ROOM_ACTION_TALK_START, NULL) == 0);
    assert(room.ptt && uplink);
    assert(xiaotai_room_action(&room, XIAOTAI_ROOM_ACTION_END, NULL) == 0);
    assert(room.assigned);
    assert(room.joined && !room.ptt && !uplink && !connected);
    xiaotai_room_handle_disconnected(&room, room.generation, 0);
    assert(!room.joined && uplink);
    assert(diagnostic_events == 1U);
    assert(last_diagnostic == XIAOTAI_ROOM_DIAG_DISCONNECTED);
    xiaotai_room_handle_response(&room, XIAOTAI_ROOM_RESPONSE_ASSIGNMENT, 0U,
                                 "{}");
    assert(diagnostic_events == 2U);
    assert(last_diagnostic == XIAOTAI_ROOM_DIAG_ASSIGNMENT_INVALID);
    return 0;
}
