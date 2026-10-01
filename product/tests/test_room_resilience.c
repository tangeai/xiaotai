#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include "xiaotai_room.h"

typedef struct {
    unsigned diagnostics;
    xiaotai_room_diagnostic_t last_diagnostic;
    unsigned media_releases;
    unsigned transport_closes;
    unsigned transport_disconnects;
    unsigned service_requests;
    unsigned assignment_requests;
    unsigned token_requests;
    unsigned presence_requests;
    unsigned media_acquires;
    unsigned transport_connects;
    unsigned media_starts;
    unsigned state_changes;
    bool uplink_enabled;
    bool media_acquire_ok;
    bool media_available;
    int service_result;
    int connect_result;
    int transport_send_result;
    int media_start_result;
    uint32_t now_ms;
} fixture_t;

static uint32_t now_ms(void *context)
{
    return ((fixture_t *)context)->now_ms;
}

static bool ready(void *context)
{
    (void)context;
    return true;
}

static bool available(void *context)
{
    return ((fixture_t *)context)->media_available;
}

static bool not_busy(void *context)
{
    (void)context;
    return false;
}

static bool connected(void *context)
{
    (void)context;
    return true;
}

static int service_request(const char *path, const char *body,
                           xiaotai_room_service_response_fn callback,
                           void *callback_context, void *context)
{
    fixture_t *fixture = context;
    (void)body;
    (void)callback;
    (void)callback_context;
    fixture->service_requests++;
    if (strstr(path, "assignment") != NULL) fixture->assignment_requests++;
    if (strstr(path, "connect-token") != NULL) fixture->token_requests++;
    if (strstr(path, "presence") != NULL) fixture->presence_requests++;
    return fixture->service_result;
}

static bool acquire_media(uint32_t *generation, void *context)
{
    fixture_t *fixture = context;
    fixture->media_acquires++;
    if (!fixture->media_acquire_ok) return false;
    *generation = 77U;
    return true;
}

static int connect_transport(const char *peer, const char *token,
                             uint32_t generation, void *context)
{
    fixture_t *fixture = context;
    assert(strcmp(peer, "peer") == 0);
    assert(strcmp(token, "token") == 0);
    assert(generation != 0U);
    fixture->transport_connects++;
    return fixture->connect_result;
}

static int transport_send(uint32_t command, const void *data, uint32_t length,
                          void *context)
{
    fixture_t *fixture = context;
    assert(command == XIAOTAI_ROOM_COMMAND);
    assert(data != NULL);
    assert(length > 0U);
    return fixture->transport_send_result;
}

static int start_media(void *context)
{
    fixture_t *fixture = context;
    fixture->media_starts++;
    return fixture->media_start_result;
}

static int disconnect_transport(void *context)
{
    fixture_t *fixture = context;
    fixture->transport_disconnects++;
    return 0;
}

static void set_uplink(bool enabled, void *context)
{
    fixture_t *fixture = context;
    fixture->uplink_enabled = enabled;
}

static void release_media(uint32_t generation, void *context)
{
    fixture_t *fixture = context;
    assert(generation == 77U);
    fixture->media_releases++;
}

static void transport_closed(void *context)
{
    fixture_t *fixture = context;
    fixture->transport_closes++;
}

static void state_changed(void *context)
{
    fixture_t *fixture = context;
    fixture->state_changes++;
}

static void diagnostic(xiaotai_room_diagnostic_t event,
                       uint32_t generation, int detail, void *context)
{
    fixture_t *fixture = context;
    (void)generation;
    (void)detail;
    fixture->diagnostics++;
    fixture->last_diagnostic = event;
}

static xiaotai_room_t active_room(fixture_t *fixture)
{
    fixture->media_available = true;
    xiaotai_room_port_t port = {
        .service_request = service_request,
        .now_ms = now_ms,
        .service_ready = ready,
        .transport_busy = not_busy,
        .transport_connected = connected,
        .transport_connect = connect_transport,
        .transport_start_media = start_media,
        .transport_send = transport_send,
        .transport_disconnect = disconnect_transport,
        .set_uplink_enabled = set_uplink,
        .media_acquire = acquire_media,
        .media_release = release_media,
        .media_available = available,
        .transport_closed = transport_closed,
        .state_changed = state_changed,
        .diagnostic = diagnostic,
        .context = fixture,
    };
    xiaotai_room_t room;
    xiaotai_room_init(&room, "device-1", &port);
    room.assigned = true;
    room.foreground = true;
    room.joined = true;
    room.generation = 9U;
    room.runtime_generation = 77U;
    strcpy(room.room_id, "room-1");
    strcpy(room.room_code, "123456");
    return room;
}

static void late_token_does_not_reacquire_media_after_menu_exit(void)
{
    fixture_t fixture = {.media_acquire_ok = true};
    xiaotai_room_t room = active_room(&fixture);
    room.joined = false;
    room.connecting = false;
    room.request_pending = true;
    assert(xiaotai_room_action(&room, XIAOTAI_ROOM_ACTION_END, NULL) == 0);

    xiaotai_room_handle_response(&room, XIAOTAI_ROOM_RESPONSE_TOKEN, 9U,
        "{\"code\":0,\"data\":{\"peer_id\":\"peer\","
        "\"token\":\"token\",\"heartbeat_seconds\":15,"
        "\"lease_seconds\":45}}");

    assert(!room.request_pending);
    assert(!room.connecting);
    assert(fixture.media_acquires == 0U);
    assert(fixture.transport_connects == 0U);
    assert(fixture.diagnostics == 0U);
}

static void transport_close_does_not_complete_pending_leave(void)
{
    fixture_t fixture = {0};
    xiaotai_room_t room = active_room(&fixture);

    assert(xiaotai_room_action(&room, XIAOTAI_ROOM_ACTION_LEAVE, NULL) == 0);
    assert(room.request_pending);
    xiaotai_room_handle_disconnected(&room, 9U, 0);
    assert(room.request_pending);

    xiaotai_room_handle_response(&room, XIAOTAI_ROOM_RESPONSE_ASSIGNMENT, 0U,
                                 "{\"code\":-1}");
    assert(!room.request_pending);
    assert(room.assigned);
    assert(fixture.last_diagnostic == XIAOTAI_ROOM_DIAG_LEAVE_FAILED);
}

static void stale_disconnect_does_not_close_new_session(void)
{
    fixture_t fixture = {.uplink_enabled = false};
    xiaotai_room_t room = active_room(&fixture);

    xiaotai_room_handle_disconnected(&room, 8U, -1);

    assert(room.joined);
    assert(room.runtime_generation == 77U);
    assert(fixture.media_releases == 0U);
    assert(fixture.transport_closes == 0U);
    assert(fixture.state_changes == 0U);
    assert(fixture.diagnostics == 1U);
    assert(fixture.last_diagnostic == XIAOTAI_ROOM_DIAG_TRANSPORT_STALE);
}

static void stale_token_does_not_complete_current_request(void)
{
    fixture_t fixture = {0};
    xiaotai_room_t room = active_room(&fixture);
    room.joined = false;
    room.connecting = true;
    room.request_pending = true;

    xiaotai_room_handle_response(&room, XIAOTAI_ROOM_RESPONSE_TOKEN, 8U,
        "{\"code\":0,\"data\":{\"peer_id\":\"old-peer\","
        "\"token\":\"old-token\"}}");

    assert(room.request_pending);
    assert(room.connecting);
    assert(room.runtime_generation == 77U);
    assert(fixture.media_releases == 0U);
    assert(fixture.diagnostics == 1U);
    assert(fixture.last_diagnostic == XIAOTAI_ROOM_DIAG_TOKEN_STALE);
}

static void stale_presence_does_not_complete_current_request(void)
{
    fixture_t fixture = {0};
    xiaotai_room_t room = active_room(&fixture);
    room.request_pending = true;
    room.next_heartbeat_ms = 1234U;
    room.lease_deadline_ms = 5678U;

    xiaotai_room_handle_response(&room, XIAOTAI_ROOM_RESPONSE_PRESENCE, 8U,
                                 "{\"code\":0,\"data\":{}}");

    assert(room.request_pending);
    assert(room.next_heartbeat_ms == 1234U);
    assert(room.lease_deadline_ms == 5678U);
    assert(room.joined);
}

static void lease_expiry_starts_only_one_disconnect(void)
{
    fixture_t fixture = {.now_ms = 1000U};
    xiaotai_room_t room = active_room(&fixture);
    strcpy(room.session_id, "session-1");
    room.lease_deadline_ms = 1000U;
    room.heartbeat_seconds = 15U;
    room.lease_seconds = 45U;

    xiaotai_room_tick(&room);
    assert(room.request_pending);
    assert(fixture.transport_disconnects == 1U);
    assert(fixture.service_requests == 1U);
    assert(fixture.diagnostics == 1U);
    assert(fixture.last_diagnostic == XIAOTAI_ROOM_DIAG_LEASE_EXPIRED);

    xiaotai_room_tick(&room);
    assert(fixture.transport_disconnects == 1U);
    assert(fixture.service_requests == 1U);
    assert(fixture.diagnostics == 1U);
}

static void invalid_token_failure_response_does_not_block_recovery(void)
{
    fixture_t fixture = {0};
    xiaotai_room_t room = active_room(&fixture);
    room.joined = false;
    room.runtime_generation = 0U;
    room.request_pending = true;
    strcpy(room.session_id, "session-1");

    xiaotai_room_handle_response(&room, XIAOTAI_ROOM_RESPONSE_TOKEN, 9U,
        "{\"code\":0,\"data\":{\"peer_id\":\"peer\"}}");
    assert(fixture.diagnostics == 1U);
    assert(fixture.last_diagnostic ==
           XIAOTAI_ROOM_DIAG_TOKEN_CONNECT_FAILED);
    assert(fixture.presence_requests == 1U);
    assert(room.request_pending);

    xiaotai_room_handle_response(&room, XIAOTAI_ROOM_RESPONSE_PRESENCE, 9U,
                                 "{\"code\":0,\"data\":{}}");
    assert(!room.request_pending);
    assert(room.assigned);
    assert(!room.joined);
}

static void assignment_request_failure_is_retryable(void)
{
    fixture_t fixture = {.service_result = -17};
    xiaotai_room_t room = active_room(&fixture);
    room.assigned = false;
    room.joined = false;
    room.runtime_generation = 0U;
    room.room_id[0] = '\0';
    room.room_code[0] = '\0';

    assert(xiaotai_room_action(&room, XIAOTAI_ROOM_ACTION_SYNC, NULL) == 0);
    xiaotai_room_tick(&room);
    assert(!room.request_pending);
    assert(fixture.assignment_requests == 1U);
    assert(fixture.diagnostics == 1U);
    assert(fixture.last_diagnostic ==
           XIAOTAI_ROOM_DIAG_ASSIGNMENT_REQUEST_FAILED);

    fixture.service_result = 0;
    assert(xiaotai_room_action(&room, XIAOTAI_ROOM_ACTION_SYNC, NULL) == 0);
    xiaotai_room_tick(&room);
    assert(room.request_pending);
    assert(fixture.assignment_requests == 2U);
}

static void duplicate_notification_coalesces_and_recovers_disconnected_room(void)
{
    fixture_t fixture = {.media_acquire_ok = true};
    xiaotai_room_t room = active_room(&fixture);

    xiaotai_room_handle_disconnected(&room, 9U, -5);
    assert(room.assigned);
    assert(!room.joined);
    assert(!room.request_pending);
    xiaotai_room_tick(&room);
    assert(fixture.assignment_requests == 0U);

    xiaotai_room_assignment_changed(&room);
    xiaotai_room_assignment_changed(&room);
    xiaotai_room_tick(&room);
    xiaotai_room_tick(&room);
    assert(fixture.assignment_requests == 1U);
    assert(room.request_pending);

    xiaotai_room_handle_response(&room, XIAOTAI_ROOM_RESPONSE_ASSIGNMENT, 0U,
        "{\"code\":0,\"data\":{\"desired_state\":\"joined\","
        "\"assignment_version\":2,\"room_id\":\"room-1\","
        "\"room_code\":\"123456\"}}");
    assert(fixture.token_requests == 1U);
    assert(room.request_pending);

    xiaotai_room_handle_response(&room, XIAOTAI_ROOM_RESPONSE_TOKEN,
        room.generation,
        "{\"code\":0,\"data\":{\"peer_id\":\"peer\","
        "\"token\":\"token\",\"heartbeat_seconds\":15,"
        "\"lease_seconds\":45}}");
    assert(fixture.media_acquires == 1U);
    assert(fixture.transport_connects == 1U);
    assert(room.connecting);
}

static void join_send_failure_disconnects_transport(void)
{
    fixture_t fixture = {.transport_send_result = -6};
    xiaotai_room_t room = active_room(&fixture);
    room.joined = false;
    room.connecting = true;

    xiaotai_room_handle_connected(&room, 9U);

    assert(fixture.diagnostics == 1U);
    assert(fixture.last_diagnostic == XIAOTAI_ROOM_DIAG_JOIN_SEND_FAILED);
    assert(fixture.transport_disconnects == 1U);
}

static void rejected_join_reports_failure_and_disconnects(void)
{
    fixture_t fixture = {0};
    xiaotai_room_t room = active_room(&fixture);
    room.joined = false;
    room.connecting = true;
    strcpy(room.session_id, "session-1");

    xiaotai_room_handle_command(&room, 9U,
        "{\"id\":1,\"error\":{\"code\":403}}");

    assert(fixture.diagnostics == 1U);
    assert(fixture.last_diagnostic == XIAOTAI_ROOM_DIAG_JOIN_REJECTED);
    assert(fixture.transport_disconnects == 1U);
}

static void media_start_failure_disconnects_join(void)
{
    fixture_t fixture = {.media_start_result = -9};
    xiaotai_room_t room = active_room(&fixture);
    room.joined = false;
    room.connecting = true;
    strcpy(room.session_id, "session-1");

    xiaotai_room_handle_command(&room, 9U,
        "{\"id\":1,\"result\":{\"session_id\":\"session\","
        "\"input_audio\":{\"codec\":\"g711a\","
        "\"sample_rate\":8000,\"channels\":1},"
        "\"output_audio\":{\"codec\":\"g711a\","
        "\"sample_rate\":8000,\"channels\":1}}}");

    assert(fixture.media_starts == 1U);
    assert(fixture.diagnostics == 1U);
    assert(fixture.last_diagnostic == XIAOTAI_ROOM_DIAG_MEDIA_START_FAILED);
    assert(fixture.transport_disconnects == 1U);
}

static void rejected_presence_clears_request_and_keeps_lease(void)
{
    fixture_t fixture = {0};
    xiaotai_room_t room = active_room(&fixture);
    room.request_pending = true;
    room.next_heartbeat_ms = 1234U;
    room.lease_deadline_ms = 5678U;

    xiaotai_room_handle_response(&room, XIAOTAI_ROOM_RESPONSE_PRESENCE, 9U,
                                 "{\"code\":503}");

    assert(!room.request_pending);
    assert(room.next_heartbeat_ms == 1234U);
    assert(room.lease_deadline_ms == 5678U);
    assert(fixture.diagnostics == 1U);
    assert(fixture.last_diagnostic == XIAOTAI_ROOM_DIAG_PRESENCE_REJECTED);
}

int main(void)
{
    stale_disconnect_does_not_close_new_session();
    stale_token_does_not_complete_current_request();
    stale_presence_does_not_complete_current_request();
    lease_expiry_starts_only_one_disconnect();
    invalid_token_failure_response_does_not_block_recovery();
    assignment_request_failure_is_retryable();
    duplicate_notification_coalesces_and_recovers_disconnected_room();
    late_token_does_not_reacquire_media_after_menu_exit();
    transport_close_does_not_complete_pending_leave();
    join_send_failure_disconnects_transport();
    rejected_join_reports_failure_and_disconnects();
    media_start_failure_disconnects_join();
    rejected_presence_clears_request_and_keeps_lease();
    return 0;
}
