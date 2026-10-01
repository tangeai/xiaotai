#include "xiaotai_room.h"

#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "cJSON.h"

#define ROOM_COMMAND 0x2200U
#define ROOM_OK 0
#define ROOM_FAIL (-1)
#define ROOM_INVALID (-2)
#define ROOM_BUSY (-3)

static uint32_t now_ms(xiaotai_room_t *room)
{
    return room->port.now_ms == NULL ? 0U :
        room->port.now_ms(room->port.context);
}

static int service_request(xiaotai_room_t *room, const char *path,
                           const char *body,
                           xiaotai_room_service_response_fn callback)
{
    return room->port.service_request == NULL ? ROOM_FAIL :
        room->port.service_request(path, body, callback, room,
                                   room->port.context);
}

static int transport_send(xiaotai_room_t *room, const void *data,
                          uint32_t length)
{
    return room->port.transport_send == NULL ? ROOM_FAIL :
        room->port.transport_send(ROOM_COMMAND, data, length,
                                  room->port.context);
}

static int transport_disconnect(xiaotai_room_t *room)
{
    return room->port.transport_disconnect == NULL ? ROOM_FAIL :
        room->port.transport_disconnect(room->port.context);
}

static const char *json_string(const cJSON *object, const char *name)
{
    const cJSON *item = cJSON_IsObject(object) ?
        cJSON_GetObjectItemCaseSensitive(object, name) : NULL;
    return cJSON_IsString(item) && item->valuestring != NULL ?
        item->valuestring : "";
}

static const char *participant_name(const cJSON *participant)
{
    const char *name = json_string(participant, "device_name");
    if (name[0] == '\0') name = json_string(participant, "display_name");
    if (name[0] == '\0') name = json_string(participant, "name");
    return name;
}

static void clear_participants(xiaotai_room_t *room)
{
    room->members = 0U;
    room->participant_count = 0U;
    memset(room->participants, 0, sizeof(room->participants));
}

static const cJSON *response_data(const cJSON *root)
{
    const cJSON *code = cJSON_IsObject(root) ?
        cJSON_GetObjectItemCaseSensitive(root, "code") : NULL;
    const cJSON *data = cJSON_IsObject(root) ?
        cJSON_GetObjectItemCaseSensitive(root, "data") : NULL;
    return cJSON_IsNumber(code) &&
           (code->valueint == 0 || code->valueint == 200) &&
           cJSON_IsObject(data) ? data : NULL;
}

static bool response_ok(const cJSON *root)
{
    const cJSON *code = cJSON_IsObject(root) ?
        cJSON_GetObjectItemCaseSensitive(root, "code") : NULL;
    return cJSON_IsNumber(code) &&
           (code->valueint == 0 || code->valueint == 200);
}

static bool audio_profile_valid(const cJSON *profile)
{
    const cJSON *codec = cJSON_IsObject(profile) ?
        cJSON_GetObjectItemCaseSensitive(profile, "codec") : NULL;
    const cJSON *rate = cJSON_IsObject(profile) ?
        cJSON_GetObjectItemCaseSensitive(profile, "sample_rate") : NULL;
    const cJSON *channels = cJSON_IsObject(profile) ?
        cJSON_GetObjectItemCaseSensitive(profile, "channels") : NULL;
    return cJSON_IsString(codec) && codec->valuestring != NULL &&
           strcmp(codec->valuestring, "g711a") == 0 &&
           cJSON_IsNumber(rate) && rate->valueint == 8000 &&
           cJSON_IsNumber(channels) && channels->valueint == 1;
}

static void changed(xiaotai_room_t *room)
{
    if (room->port.state_changed != NULL) {
        room->port.state_changed(room->port.context);
    }
}

static void diagnostic(xiaotai_room_t *room,
                       xiaotai_room_diagnostic_t event, int detail)
{
    if (room->port.diagnostic != NULL) {
        room->port.diagnostic(event, room->generation, detail,
                              room->port.context);
    }
}

static void response_callback(const char *body, void *context)
{
    xiaotai_room_t *room = context;
    if (room->port.post_response != NULL) {
        room->port.post_response(XIAOTAI_ROOM_RESPONSE_ASSIGNMENT, 0U, body,
            body == NULL ? 0U : strlen(body), room->port.context);
    }
}

static void token_callback(const char *body, void *context)
{
    xiaotai_room_t *room = context;
    if (room->port.post_response != NULL) {
        room->port.post_response(XIAOTAI_ROOM_RESPONSE_TOKEN,
            room->generation, body, body == NULL ? 0U : strlen(body),
            room->port.context);
    }
}

static void presence_callback(const char *body, void *context)
{
    xiaotai_room_t *room = context;
    if (room->port.post_response != NULL) {
        room->port.post_response(XIAOTAI_ROOM_RESPONSE_PRESENCE,
            room->generation, body, body == NULL ? 0U : strlen(body),
            room->port.context);
    }
}

static int presence(xiaotai_room_t *room, const char *state)
{
    if (!room->assigned || room->room_id[0] == '\0' ||
        room->session_id[0] == '\0') return ROOM_FAIL;
    char request[320];
    int length = snprintf(request, sizeof(request),
        "{\"room_id\":\"%s\",\"assignment_version\":%lld,"
        "\"session_id\":\"%s\",\"state\":\"%s\"}",
        room->room_id, (long long)room->assignment_version,
        room->session_id, state);
    int rc = length > 0 && (size_t)length < sizeof(request) ?
        service_request(room, "/v1/call/group/device/presence", request,
                        presence_callback) : ROOM_FAIL;
    if (rc == 0) room->request_pending = true;
    return rc;
}

static void request_assignment(xiaotai_room_t *room)
{
    room->assignment_refresh_requested = false;
    room->request_pending = true;
    int rc = service_request(room, "/v1/call/group/device/assignment", NULL,
                             response_callback);
    if (rc != 0) {
        room->request_pending = false;
        diagnostic(room, XIAOTAI_ROOM_DIAG_ASSIGNMENT_REQUEST_FAILED, rc);
    }
}

static void new_session_id(xiaotai_room_t *room)
{
    uint32_t words[8];
    if (room->port.random_bytes == NULL ||
        !room->port.random_bytes(words, sizeof(words), room->port.context)) {
        uint32_t seed = now_ms(room) ^ room->generation ^
                        (uint32_t)(uintptr_t)room;
        for (size_t i = 0; i < 8U; ++i) {
            seed = seed * 1664525U + 1013904223U;
            words[i] = seed;
        }
    }
    for (size_t i = 0; i < 8U; ++i) {
        (void)snprintf(room->session_id + i * 8U,
                       sizeof(room->session_id) - i * 8U,
                       "%08x", (unsigned)words[i]);
    }
    room->session_id[64] = '\0';
}

static void request_token(xiaotai_room_t *room)
{
    new_session_id(room);
    char request[288];
    int length = snprintf(request, sizeof(request),
        "{\"room_id\":\"%s\",\"assignment_version\":%lld,"
        "\"session_id\":\"%s\"}", room->room_id,
        (long long)room->assignment_version, room->session_id);
    room->request_pending = true;
    int rc = length > 0 && (size_t)length < sizeof(request) ?
        service_request(room, "/v1/call/group/device/connect-token", request,
                        token_callback) : ROOM_FAIL;
    if (rc != 0) {
        room->request_pending = false;
    }
}

static void set_ptt(xiaotai_room_t *room, bool enabled)
{
    if (!room->joined || room->ptt == enabled) return;
    room->ptt = enabled;
    if (room->port.set_uplink_enabled != NULL) {
        room->port.set_uplink_enabled(enabled, room->port.context);
    }
    char command[112];
    int length = snprintf(command, sizeof(command),
        "{\"jsonrpc\":\"2.0\",\"method\":\"set_mic_state\","
        "\"params\":{\"mic_state\":\"%s\"}}",
        enabled ? "speaking" : "off");
    if (length > 0 && (size_t)length < sizeof(command)) {
        (void)transport_send(room, command, (uint32_t)length);
    }
    changed(room);
}

void xiaotai_room_init(xiaotai_room_t *room, const char *device_id,
                       const xiaotai_room_port_t *port)
{
    if (room == NULL) return;
    memset(room, 0, sizeof(*room));
    if (port != NULL) room->port = *port;
    if (device_id != NULL) {
        snprintf(room->device_id, sizeof(room->device_id), "%s", device_id);
    }
}

void xiaotai_room_set_device_id(xiaotai_room_t *room, const char *device_id)
{
    if (room == NULL || device_id == NULL) return;
    snprintf(room->device_id, sizeof(room->device_id), "%s", device_id);
}

bool xiaotai_room_active(const xiaotai_room_t *room)
{
    return room != NULL && (room->connecting || room->joined);
}

bool xiaotai_room_joined(const xiaotai_room_t *room)
{
    return room != NULL && room->joined;
}

void xiaotai_room_snapshot(const xiaotai_room_t *room,
                           xiaotai_room_snapshot_t *out)
{
    if (room == NULL || out == NULL) return;
    *out = (xiaotai_room_snapshot_t) {
        .assigned = room->assigned,
        .request_pending = room->request_pending,
        .connecting = room->connecting,
        .joined = room->joined,
        .talking = room->ptt,
        .members = room->members,
        .participant_count = room->participant_count,
        .assignment_version = room->assignment_version,
    };
    memcpy(out->participants, room->participants, sizeof(out->participants));
    snprintf(out->room_code, sizeof(out->room_code), "%s", room->room_code);
}

void xiaotai_room_disconnect(xiaotai_room_t *room, const char *state)
{
    if (room == NULL || !xiaotai_room_active(room)) return;
    set_ptt(room, false);
    if (room->port.transport_connected != NULL &&
        room->port.transport_connected(room->port.context)) {
        static const char off[] =
            "{\"jsonrpc\":\"2.0\",\"method\":\"set_mic_state\","
            "\"params\":{\"mic_state\":\"off\"}}";
        static const char leave[] =
            "{\"jsonrpc\":\"2.0\",\"method\":\"leave_room\"}";
        (void)transport_send(room, off, sizeof(off) - 1U);
        (void)transport_send(room, leave, sizeof(leave) - 1U);
    }
    if (state != NULL) (void)presence(room, state);
    (void)transport_disconnect(room);
}

static void handle_assignment(xiaotai_room_t *room, const char *text)
{
    room->request_pending = false;
    cJSON *root = text == NULL ? NULL : cJSON_Parse(text);
    const cJSON *data = response_data(root);
    const cJSON *version = data == NULL ? NULL :
        cJSON_GetObjectItemCaseSensitive(data, "assignment_version");
    const char *room_id = json_string(data, "room_id");
    const char *room_code = json_string(data, "room_code");
    bool desired = strcmp(json_string(data, "desired_state"), "joined") == 0;
    bool valid = data != NULL && cJSON_IsNumber(version) &&
                 version->valuedouble >= 0 && strlen(room_id) < 65U &&
                 strlen(room_code) < 7U &&
                 (!desired || (room_id[0] != '\0' && strlen(room_code) == 6U));
    if (!valid) {
        diagnostic(room, room->leave_pending ? XIAOTAI_ROOM_DIAG_LEAVE_FAILED :
                                              XIAOTAI_ROOM_DIAG_ASSIGNMENT_INVALID,
                   0);
        room->leave_pending = false;
        cJSON_Delete(root);
        changed(room);
        return;
    }
    room->leave_pending = false;
    int64_t new_version = (int64_t)version->valuedouble;
    bool assignment_changed = new_version != room->assignment_version ||
                              strcmp(room_id, room->room_id) != 0;
    if ((!desired || assignment_changed) && xiaotai_room_active(room)) {
        xiaotai_room_disconnect(room, desired ? "suspended" : "left");
    }
    room->assigned = desired && room_id[0] != '\0';
    room->assignment_version = new_version;
    snprintf(room->room_id, sizeof(room->room_id), "%s", room_id);
    snprintf(room->room_code, sizeof(room->room_code), "%s", room_code);
    if (assignment_changed) {
        room->joined = false;
        room->connecting = false;
        clear_participants(room);
        room->session_id[0] = '\0';
        if (++room->generation == 0U) room->generation = 1U;
    }
    cJSON_Delete(root);
    changed(room);
    if (room->assigned && room->foreground &&
        room->port.media_available != NULL &&
        room->port.media_available(room->port.context) &&
        room->port.transport_busy != NULL &&
        !room->port.transport_busy(room->port.context) &&
        !room->connecting) request_token(room);
}

static void handle_token(xiaotai_room_t *room, uint32_t generation,
                         const char *text)
{
    if (!room->assigned || generation != room->generation) {
        diagnostic(room, XIAOTAI_ROOM_DIAG_TOKEN_STALE, (int)generation);
        return;
    }
    room->request_pending = false;
    if (!room->foreground ||
        (room->port.media_available != NULL &&
         !room->port.media_available(room->port.context))) {
        changed(room);
        return;
    }
    cJSON *root = text == NULL ? NULL : cJSON_Parse(text);
    const cJSON *data = response_data(root);
    const char *peer = json_string(data, "peer_id");
    const char *token = json_string(data, "token");
    const cJSON *heartbeat = data == NULL ? NULL :
        cJSON_GetObjectItemCaseSensitive(data, "heartbeat_seconds");
    const cJSON *lease = data == NULL ? NULL :
        cJSON_GetObjectItemCaseSensitive(data, "lease_seconds");
    bool valid = peer[0] != '\0' && token[0] != '\0';
    room->heartbeat_seconds = cJSON_IsNumber(heartbeat) &&
        heartbeat->valueint > 0 ? (uint32_t)heartbeat->valueint : 15U;
    room->lease_seconds = cJSON_IsNumber(lease) && lease->valueint >= 3 ?
        (uint32_t)lease->valueint : 45U;
    bool acquired = valid && room->port.media_acquire != NULL &&
        room->port.media_acquire(&room->runtime_generation,
                                 room->port.context);
    int rc = acquired && room->port.transport_connect != NULL ?
        room->port.transport_connect(peer, token, room->generation,
                                     room->port.context) : ROOM_FAIL;
    cJSON_Delete(root);
    if (rc == 0) {
        room->connecting = true;
        changed(room);
    } else {
        if (acquired && room->port.media_release != NULL) {
            room->port.media_release(room->runtime_generation,
                                     room->port.context);
        }
        room->runtime_generation = 0U;
        diagnostic(room, XIAOTAI_ROOM_DIAG_TOKEN_CONNECT_FAILED, rc);
        (void)presence(room, "connect_failed");
    }
}

static void handle_presence(xiaotai_room_t *room, uint32_t generation,
                            const char *text)
{
    if (generation != room->generation) return;
    room->request_pending = false;
    if (!room->joined) return;
    cJSON *root = text == NULL ? NULL : cJSON_Parse(text);
    bool ok = response_ok(root);
    cJSON_Delete(root);
    if (!ok) {
        diagnostic(room, XIAOTAI_ROOM_DIAG_PRESENCE_REJECTED, 0);
        return;
    }
    uint32_t now = now_ms(room);
    room->next_heartbeat_ms = now + room->heartbeat_seconds * 1000U;
    room->lease_deadline_ms = now + room->lease_seconds * 1000U;
}

void xiaotai_room_handle_response(xiaotai_room_t *room,
                                  xiaotai_room_response_t type,
                                  uint32_t generation, const char *text)
{
    if (room == NULL) return;
    if (type == XIAOTAI_ROOM_RESPONSE_ASSIGNMENT) {
        handle_assignment(room, text);
    } else if (type == XIAOTAI_ROOM_RESPONSE_TOKEN) {
        handle_token(room, generation, text);
    } else if (type == XIAOTAI_ROOM_RESPONSE_PRESENCE) {
        handle_presence(room, generation, text);
    }
}

void xiaotai_room_handle_connected(xiaotai_room_t *room, uint32_t generation)
{
    if (room == NULL) return;
    if (!room->assigned || generation != room->generation) {
        diagnostic(room, XIAOTAI_ROOM_DIAG_TRANSPORT_STALE,
                   (int)generation);
        (void)transport_disconnect(room);
        return;
    }
    char command[384];
    int length = snprintf(command, sizeof(command),
        "{\"jsonrpc\":\"2.0\",\"id\":1,\"method\":\"join_room\","
        "\"params\":{\"room_id\":\"%s\",\"device_id\":\"%s\","
        "\"input_audio\":{\"codec\":\"g711a\",\"sample_rate\":8000,\"channels\":1},"
        "\"output_audio\":{\"codec\":\"g711a\",\"sample_rate\":8000,\"channels\":1}}}",
        room->room_id, room->device_id);
    if (length <= 0 || (size_t)length >= sizeof(command) ||
        transport_send(room, command, (uint32_t)length) < 0) {
        diagnostic(room, XIAOTAI_ROOM_DIAG_JOIN_SEND_FAILED, 0);
        (void)transport_disconnect(room);
        return;
    }
    room->connecting = true;
    room->lease_deadline_ms = now_ms(room) + 10000U;
}

void xiaotai_room_handle_command(xiaotai_room_t *room, uint32_t generation,
                                 const char *text)
{
    if (room == NULL || generation != room->generation || text == NULL) return;
    cJSON *root = cJSON_Parse(text);
    const cJSON *id = root == NULL ? NULL :
        cJSON_GetObjectItemCaseSensitive(root, "id");
    const cJSON *result = root == NULL ? NULL :
        cJSON_GetObjectItemCaseSensitive(root, "result");
    const char *method = json_string(root, "method");
    const cJSON *params = root == NULL ? NULL :
        cJSON_GetObjectItemCaseSensitive(root, "params");
    const cJSON *input = cJSON_IsObject(result) ?
        cJSON_GetObjectItemCaseSensitive(result, "input_audio") : NULL;
    const cJSON *output = cJSON_IsObject(result) ?
        cJSON_GetObjectItemCaseSensitive(result, "output_audio") : NULL;
    bool join_accepted = cJSON_IsNumber(id) && id->valueint == 1 &&
        cJSON_IsObject(result) && json_string(result, "session_id")[0] != '\0' &&
        audio_profile_valid(input) && audio_profile_valid(output);
    if (join_accepted) {
        int rc = room->port.transport_start_media == NULL ? ROOM_FAIL :
            room->port.transport_start_media(room->port.context);
        bool active = rc == ROOM_OK && room->port.media_activate != NULL &&
            room->port.media_activate(room->runtime_generation,
                                      room->port.context);
        if (!active) {
            diagnostic(room, XIAOTAI_ROOM_DIAG_MEDIA_START_FAILED, rc);
            cJSON_Delete(root);
            xiaotai_room_disconnect(room, "connect_failed");
            return;
        }
        room->connecting = false;
        room->joined = true;
        room->ptt = false;
        if (room->port.set_uplink_enabled != NULL) {
            room->port.set_uplink_enabled(false, room->port.context);
        }
        uint32_t now = now_ms(room);
        room->next_heartbeat_ms = now + room->heartbeat_seconds * 1000U;
        room->lease_deadline_ms = now + room->lease_seconds * 1000U;
        (void)presence(room, "joined");
        static const char off[] =
            "{\"jsonrpc\":\"2.0\",\"method\":\"set_mic_state\","
            "\"params\":{\"mic_state\":\"off\"}}";
        static const char snapshot[] =
            "{\"jsonrpc\":\"2.0\",\"method\":\"get_room_snapshot\"}";
        (void)transport_send(room, off, sizeof(off) - 1U);
        (void)transport_send(room, snapshot, sizeof(snapshot) - 1U);
    } else if (cJSON_IsNumber(id) && id->valueint == 1) {
        diagnostic(room, XIAOTAI_ROOM_DIAG_JOIN_REJECTED, 0);
        cJSON_Delete(root);
        xiaotai_room_disconnect(room, "connect_failed");
        return;
    } else if (strcmp(method, "room_snapshot") == 0) {
        const cJSON *participants = cJSON_IsObject(params) ?
            cJSON_GetObjectItemCaseSensitive(params, "participants") : NULL;
        if (cJSON_IsArray(participants)) {
            int count = cJSON_GetArraySize(participants);
            room->members = count > 0 ? (unsigned)count : 0U;
            room->participant_count = count > 0 ? (size_t)count : 0U;
            if (room->participant_count > XIAOTAI_ROOM_PARTICIPANTS_MAX) {
                room->participant_count = XIAOTAI_ROOM_PARTICIPANTS_MAX;
            }
            memset(room->participants, 0, sizeof(room->participants));
            for (size_t i = 0U; i < room->participant_count; ++i) {
                const cJSON *participant = cJSON_GetArrayItem(
                    participants, (int)i);
                const char *device_id = json_string(participant, "device_id");
                const char *participant_id = json_string(
                    participant, "participant_id");
                const char *id_value = device_id[0] != '\0' ? device_id :
                    participant_id;
                const char *name = participant_name(participant);
                snprintf(room->participants[i].id,
                         sizeof(room->participants[i].id), "%s", id_value);
                snprintf(room->participants[i].name,
                         sizeof(room->participants[i].name), "%s",
                         name[0] != '\0' ? name : id_value);
                room->participants[i].self = device_id[0] != '\0' &&
                    strcmp(device_id, room->device_id) == 0;
                room->participants[i].speaking = strcmp(
                    json_string(participant, "mic_state"), "speaking") == 0;
            }
        }
    } else if (strcmp(method, "participant_joined") == 0 ||
               strcmp(method, "participant_left") == 0 ||
               strcmp(method, "participant_mic_state_changed") == 0) {
        static const char snapshot[] =
            "{\"jsonrpc\":\"2.0\",\"method\":\"get_room_snapshot\"}";
        (void)transport_send(room, snapshot, sizeof(snapshot) - 1U);
    } else if (strcmp(method, "room_closed") == 0) {
        xiaotai_room_disconnect(room, "left");
        room->assigned = false;
    }
    cJSON_Delete(root);
    changed(room);
}

void xiaotai_room_handle_disconnected(xiaotai_room_t *room,
                                      uint32_t generation, int error)
{
    if (room == NULL) return;
    if (generation != room->generation) {
        diagnostic(room, XIAOTAI_ROOM_DIAG_TRANSPORT_STALE,
                   (int)generation);
        return;
    }
    diagnostic(room, XIAOTAI_ROOM_DIAG_DISCONNECTED, error);
    room->connecting = false;
    room->joined = false;
    room->request_pending = room->leave_pending;
    room->ptt = false;
    clear_participants(room);
    if (room->port.set_uplink_enabled != NULL) {
        room->port.set_uplink_enabled(true, room->port.context);
    }
    if (room->runtime_generation != 0U && room->port.media_release != NULL) {
        room->port.media_release(room->runtime_generation, room->port.context);
    }
    room->runtime_generation = 0U;
    changed(room);
    if (room->port.transport_closed != NULL) {
        room->port.transport_closed(room->port.context);
    }
}

int xiaotai_room_action(xiaotai_room_t *room, xiaotai_room_action_t action,
                        const char *json)
{
    if (room == NULL) return ROOM_INVALID;
    if (action == XIAOTAI_ROOM_ACTION_TALK_START ||
        action == XIAOTAI_ROOM_ACTION_TALK_STOP) {
        if (!room->joined) return ROOM_FAIL;
        set_ptt(room, action == XIAOTAI_ROOM_ACTION_TALK_START);
        return ROOM_OK;
    }
    if (action == XIAOTAI_ROOM_ACTION_SYNC) {
        room->foreground = true;
        room->assignment_refresh_requested = true;
        return ROOM_OK;
    }
    if (action == XIAOTAI_ROOM_ACTION_END) {
        room->foreground = false;
        xiaotai_room_disconnect(room, "suspended");
        return ROOM_OK;
    }
    if (room->request_pending) return ROOM_BUSY;
    if (action == XIAOTAI_ROOM_ACTION_CREATE ||
        action == XIAOTAI_ROOM_ACTION_JOIN) {
        if (room->assigned || json == NULL) return ROOM_INVALID;
        room->foreground = true;
        room->request_pending = true;
        const char *path = action == XIAOTAI_ROOM_ACTION_CREATE ?
            "/v1/call/group/device/create" :
            "/v1/call/group/device/join";
        int rc = service_request(room, path, json, response_callback);
        if (rc != ROOM_OK) room->request_pending = false;
        changed(room);
        return rc;
    }
    if (action == XIAOTAI_ROOM_ACTION_LEAVE) {
        char request[192];
        if (room->assigned) {
            snprintf(request, sizeof(request),
                     "{\"room_id\":\"%s\",\"assignment_version\":%lld}",
                     room->room_id, (long long)room->assignment_version);
        } else {
            snprintf(request, sizeof(request), "{}");
        }
        room->foreground = false;
        room->leave_pending = true;
        xiaotai_room_disconnect(room, NULL);
        room->request_pending = true;
        int rc = service_request(room, "/v1/call/group/device/leave", request,
                                 response_callback);
        if (rc != ROOM_OK) {
            room->request_pending = false;
            room->leave_pending = false;
        }
        changed(room);
        return rc;
    }
    return ROOM_INVALID;
}

void xiaotai_room_assignment_changed(xiaotai_room_t *room)
{
    if (room != NULL) room->assignment_refresh_requested = true;
}

void xiaotai_room_closed(xiaotai_room_t *room)
{
    if (room == NULL) return;
    xiaotai_room_disconnect(room, "left");
    room->assigned = false;
    changed(room);
}

void xiaotai_room_tick(xiaotai_room_t *room)
{
    if (room == NULL || room->port.service_ready == NULL ||
        !room->port.service_ready(room->port.context)) return;
    /* Assignment reconciliation is background work. Do not overlap its TLS
     * handshake with an unrelated STREAM/AI/call connection or with the
     * bounded cleanup window after an outgoing connection fails. An active
     * room still needs its own presence and assignment traffic. */
    if (!xiaotai_room_active(room) && room->port.transport_busy != NULL &&
        room->port.transport_busy(room->port.context)) return;
    uint32_t now = now_ms(room);
    bool available = room->foreground &&
                     room->port.media_available != NULL &&
                     room->port.media_available(room->port.context);
    if (!available) {
        if (xiaotai_room_active(room)) {
            xiaotai_room_disconnect(room, "suspended");
        }
        return;
    }
    if (room->joined) {
        if (!room->request_pending &&
            (int32_t)(now - room->lease_deadline_ms) >= 0) {
            diagnostic(room, XIAOTAI_ROOM_DIAG_LEASE_EXPIRED, 0);
            xiaotai_room_disconnect(room, "connect_failed");
        } else if (!room->request_pending &&
                   (int32_t)(now - room->next_heartbeat_ms) >= 0) {
            if (presence(room, "joined") == 0) {
                room->next_heartbeat_ms = now +
                    room->heartbeat_seconds * 1000U;
            }
        } else if (!room->request_pending &&
                   room->assignment_refresh_requested) {
            request_assignment(room);
        }
        return;
    }
    if (room->request_pending || room->connecting ||
        !room->assignment_refresh_requested) return;
    request_assignment(room);
}
