#include "xiaotai_ai_protocol.h"

#include <limits.h>
#include <stdio.h>
#include <string.h>

#include "cJSON.h"

static bool audio_profile_valid(const cJSON *profile)
{
    const cJSON *codec = cJSON_IsObject(profile) ?
        cJSON_GetObjectItemCaseSensitive(profile, "codec") : NULL;
    const cJSON *rate = cJSON_IsObject(profile) ?
        cJSON_GetObjectItemCaseSensitive(profile, "sample_rate") : NULL;
    const cJSON *channels = cJSON_IsObject(profile) ?
        cJSON_GetObjectItemCaseSensitive(profile, "channels") : NULL;
    return cJSON_IsString(codec) && codec->valuestring != NULL &&
           strcmp(codec->valuestring, "opus") == 0 &&
           cJSON_IsNumber(rate) && rate->valueint == 16000 &&
           cJSON_IsNumber(channels) && channels->valueint == 1;
}

int xiaotai_ai_encode_start(char *output, size_t capacity,
                            const char *request_id, const char *device_id,
                            const char *role_id)
{
    if (output == NULL || capacity == 0U || request_id == NULL ||
        request_id[0] == '\0' || device_id == NULL || device_id[0] == '\0' ||
        role_id == NULL || role_id[0] == '\0') return -1;
    int length = snprintf(output, capacity,
        "{\"jsonrpc\":\"2.0\",\"id\":\"%s\",\"method\":\"start_session\","
        "\"params\":{\"device_id\":\"%s\",\"role_id\":\"%s\","
        "\"input_audio\":{\"codec\":\"opus\",\"sample_rate\":16000,\"channels\":1},"
        "\"output_audio\":{\"codec\":\"opus\",\"sample_rate\":16000,\"channels\":1}}}",
        request_id, device_id, role_id);
    return length > 0 && (size_t)length < capacity ? length : -1;
}

xiaotai_ai_message_t xiaotai_ai_decode_message(const char *json,
                                                size_t length,
                                                const char *request_id)
{
    if (json == NULL || length == 0U || request_id == NULL) {
        return XIAOTAI_AI_MESSAGE_INVALID;
    }
    cJSON *root = cJSON_ParseWithLength(json, length);
    if (!cJSON_IsObject(root)) {
        cJSON_Delete(root);
        return XIAOTAI_AI_MESSAGE_INVALID;
    }
    const cJSON *method = cJSON_GetObjectItemCaseSensitive(root, "method");
    if (cJSON_IsString(method) && method->valuestring != NULL) {
        xiaotai_ai_message_t type = XIAOTAI_AI_MESSAGE_IGNORE;
        if (strcmp(method->valuestring, "call_intent") == 0 ||
            strcmp(method->valuestring, "ai_call_intent") == 0 ||
            strcmp(method->valuestring, "device_action") == 0) {
            type = XIAOTAI_AI_MESSAGE_CALL_INTENT;
        } else if (strcmp(method->valuestring, "caption") == 0 ||
                   strcmp(method->valuestring, "round_start") == 0 ||
                   strcmp(method->valuestring, "round_end") == 0 ||
                   strcmp(method->valuestring, "emotion") == 0 ||
                   strcmp(method->valuestring, "ai_emotion") == 0 ||
                   strcmp(method->valuestring, "expression") == 0 ||
                   strcmp(method->valuestring, "event") == 0) {
            type = XIAOTAI_AI_MESSAGE_UI;
        } else if (strcmp(method->valuestring, "end_session") == 0) {
            type = XIAOTAI_AI_MESSAGE_END;
        }
        cJSON_Delete(root);
        return type;
    }

    const cJSON *legacy_type = cJSON_GetObjectItemCaseSensitive(root, "type");
    if (cJSON_IsString(legacy_type) && legacy_type->valuestring != NULL &&
        (strcmp(legacy_type->valuestring, "ai_reply") == 0 ||
         strcmp(legacy_type->valuestring, "AI_EMOTION") == 0 ||
         strcmp(legacy_type->valuestring, "ai_emotion") == 0)) {
        cJSON_Delete(root);
        return XIAOTAI_AI_MESSAGE_UI;
    }

    const cJSON *id = cJSON_GetObjectItemCaseSensitive(root, "id");
    bool matches = cJSON_IsString(id) && id->valuestring != NULL &&
                   strcmp(id->valuestring, request_id) == 0;
    if (!matches) {
        cJSON_Delete(root);
        return XIAOTAI_AI_MESSAGE_IGNORE;
    }
    if (cJSON_GetObjectItemCaseSensitive(root, "error") != NULL) {
        cJSON_Delete(root);
        return XIAOTAI_AI_MESSAGE_START_REJECTED;
    }
    const cJSON *result = cJSON_GetObjectItemCaseSensitive(root, "result");
    const cJSON *session = cJSON_IsObject(result) ?
        cJSON_GetObjectItemCaseSensitive(result, "session_id") : NULL;
    const cJSON *input = cJSON_IsObject(result) ?
        cJSON_GetObjectItemCaseSensitive(result, "input_audio") : NULL;
    const cJSON *output = cJSON_IsObject(result) ?
        cJSON_GetObjectItemCaseSensitive(result, "output_audio") : NULL;
    bool accepted = cJSON_IsString(session) && session->valuestring != NULL &&
                    session->valuestring[0] != '\0' &&
                    audio_profile_valid(input) && audio_profile_valid(output);
    cJSON_Delete(root);
    return accepted ? XIAOTAI_AI_MESSAGE_START_ACCEPTED :
                      XIAOTAI_AI_MESSAGE_START_REJECTED;
}

static void add_optional_string(cJSON *object, const char *name,
                                const char *value)
{
    if (value != NULL && value[0] != '\0') {
        cJSON_AddStringToObject(object, name, value);
    }
}

int xiaotai_ai_encode_call_action_result(
    char *output, size_t capacity, const char *request_json, bool ok,
    const char *status, const char *message, const char *contact_type,
    const char *target_device_id, const char *matched_name)
{
    if (output == NULL || capacity == 0U || capacity > (size_t)INT_MAX ||
        request_json == NULL) {
        return -1;
    }
    output[0] = '\0';
    cJSON *request = cJSON_Parse(request_json);
    bool parsed = cJSON_IsObject(request);
    const cJSON *method = cJSON_IsObject(request) ?
        cJSON_GetObjectItemCaseSensitive(request, "method") : NULL;
    if (!cJSON_IsString(method) || method->valuestring == NULL ||
        strcmp(method->valuestring, "device_action") != 0) {
        cJSON_Delete(request);
        return parsed ? 0 : -1;
    }
    const cJSON *request_id =
        cJSON_GetObjectItemCaseSensitive(request, "id");
    if ((!cJSON_IsString(request_id) && !cJSON_IsNumber(request_id)) ||
        (cJSON_IsString(request_id) && request_id->valuestring == NULL)) {
        cJSON_Delete(request);
        return -1;
    }

    cJSON *root = cJSON_CreateObject();
    cJSON *id = cJSON_Duplicate(request_id, true);
    cJSON *body = cJSON_CreateObject();
    if (root == NULL || id == NULL || body == NULL) {
        cJSON_Delete(request);
        cJSON_Delete(root);
        cJSON_Delete(id);
        cJSON_Delete(body);
        return -1;
    }
    cJSON_AddStringToObject(root, "jsonrpc", "2.0");
    cJSON_AddItemToObject(root, "id", id);
    if (ok) {
        cJSON_AddBoolToObject(body, "ok", true);
        cJSON_AddStringToObject(body, "status",
                               status != NULL && status[0] != '\0' ?
                                   status : "ok");
        cJSON_AddStringToObject(body, "message",
                               message != NULL && message[0] != '\0' ?
                                   message : "已开始处理");
        add_optional_string(body, "contact_type", contact_type);
        add_optional_string(body, "target_device_id", target_device_id);
        add_optional_string(body, "matched_name", matched_name);
        cJSON_AddItemToObject(root, "result", body);
    } else {
        cJSON *data = cJSON_CreateObject();
        if (data == NULL) {
            cJSON_Delete(request);
            cJSON_Delete(root);
            cJSON_Delete(body);
            return -1;
        }
        cJSON_AddNumberToObject(body, "code", -32012);
        cJSON_AddStringToObject(body, "message",
                               message != NULL && message[0] != '\0' ?
                                   message : "未找到联系人");
        cJSON_AddStringToObject(data, "status",
                               status != NULL && status[0] != '\0' ?
                                   status : "not_found");
        cJSON_AddBoolToObject(data, "ok", false);
        cJSON_AddItemToObject(body, "data", data);
        cJSON_AddItemToObject(root, "error", body);
    }

    bool printed = cJSON_PrintPreallocated(root, output, (int)capacity, false);
    cJSON_Delete(request);
    cJSON_Delete(root);
    return printed ? (int)strlen(output) : -1;
}

bool xiaotai_ai_accepts_command(uint32_t command, uint32_t response_bit)
{
    return ((command & 0xffffU) & ~response_bit) == XIAOTAI_AI_COMMAND;
}

void xiaotai_ai_end_drain_begin(xiaotai_ai_end_drain_t *drain,
                                uint32_t generation, uint32_t now_ms,
                                uint32_t arrival_grace_ms,
                                uint32_t timeout_ms)
{
    if (drain == NULL) return;
    drain->pending = true;
    drain->accepted = true;
    drain->generation = generation;
    drain->disconnect_not_before_ms = now_ms + arrival_grace_ms;
    drain->deadline_ms = now_ms + timeout_ms;
}

void xiaotai_ai_end_drain_cancel(xiaotai_ai_end_drain_t *drain)
{
    if (drain == NULL) return;
    drain->pending = false;
    drain->accepted = false;
}

bool xiaotai_ai_end_drain_accepts_remote_close(
    const xiaotai_ai_end_drain_t *drain, uint32_t generation)
{
    return drain != NULL && drain->accepted &&
           drain->generation == generation;
}

bool xiaotai_ai_remote_close_is_normal(
    const xiaotai_ai_end_drain_t *drain, uint32_t generation,
    bool media_was_active)
{
    return media_was_active ||
           xiaotai_ai_end_drain_accepts_remote_close(drain, generation);
}

xiaotai_ai_end_drain_result_t xiaotai_ai_end_drain_step(
    xiaotai_ai_end_drain_t *drain, uint32_t generation, uint32_t now_ms,
    bool playback_drained)
{
    if (drain == NULL || !drain->pending) {
        return XIAOTAI_AI_END_DRAIN_WAIT;
    }
    if (drain->generation != generation) {
        return XIAOTAI_AI_END_DRAIN_STALE;
    }
    bool timed_out = (int32_t)(now_ms - drain->deadline_ms) >= 0;
    bool arrival_grace_elapsed =
        (int32_t)(now_ms - drain->disconnect_not_before_ms) >= 0;
    if (!timed_out && (!arrival_grace_elapsed || !playback_drained)) {
        return XIAOTAI_AI_END_DRAIN_WAIT;
    }
    drain->pending = false;
    return XIAOTAI_AI_END_DRAIN_DISCONNECT;
}
