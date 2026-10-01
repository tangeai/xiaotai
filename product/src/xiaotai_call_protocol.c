#include "xiaotai_call_protocol.h"

#include <stdio.h>
#include <string.h>

#include "cJSON.h"

static const char *json_string(const cJSON *object, const char *name)
{
    const cJSON *item = cJSON_IsObject(object) ?
        cJSON_GetObjectItemCaseSensitive(object, name) : NULL;
    return cJSON_IsString(item) && item->valuestring != NULL ?
        item->valuestring : "";
}

static bool copy_bounded(char *output, size_t capacity, const char *value)
{
    if (output == NULL || capacity == 0U || value == NULL ||
        value[0] == '\0' || strlen(value) >= capacity) return false;
    memcpy(output, value, strlen(value) + 1U);
    return true;
}

bool xiaotai_call_decode_info(const char *json, char *peer, size_t peer_size,
                              char *token, size_t token_size)
{
    if (json == NULL) return false;
    cJSON *root = cJSON_Parse(json);
    const cJSON *code = cJSON_IsObject(root) ?
        cJSON_GetObjectItemCaseSensitive(root, "code") : NULL;
    const cJSON *data = cJSON_IsObject(root) ?
        cJSON_GetObjectItemCaseSensitive(root, "data") : NULL;
    bool valid = cJSON_IsNumber(code) && code->valueint == 200 &&
        copy_bounded(peer, peer_size, json_string(data, "device_id")) &&
        copy_bounded(token, token_size, json_string(data, "token"));
    cJSON_Delete(root);
    return valid;
}

xiaotai_call_outbound_result_t xiaotai_call_decode_outbound_result(
    const char *json, bool wechat, char *identifier, size_t identifier_size)
{
    if (json == NULL) return XIAOTAI_CALL_OUTBOUND_INVALID;
    cJSON *root = cJSON_Parse(json);
    const cJSON *code = cJSON_IsObject(root) ?
        cJSON_GetObjectItemCaseSensitive(root, "code") : NULL;
    const cJSON *data = cJSON_IsObject(root) ?
        cJSON_GetObjectItemCaseSensitive(root, "data") : NULL;
    xiaotai_call_outbound_result_t result = XIAOTAI_CALL_OUTBOUND_INVALID;
    if (cJSON_IsNumber(code)) {
        if (code->valueint == 40201) {
            result = XIAOTAI_CALL_OUTBOUND_OFFLINE;
        } else if (code->valueint == 0 || code->valueint == 200) {
            result = copy_bounded(identifier, identifier_size,
                                  json_string(data, wechat ? "call_id" :
                                              "room_id")) ?
                XIAOTAI_CALL_OUTBOUND_ACCEPTED :
                XIAOTAI_CALL_OUTBOUND_INVALID;
        } else {
            result = XIAOTAI_CALL_OUTBOUND_FAILED;
        }
    }
    cJSON_Delete(root);
    return result;
}

bool xiaotai_call_decode_outbound(const char *json, bool wechat,
                                  char *identifier, size_t identifier_size)
{
    return xiaotai_call_decode_outbound_result(
        json, wechat, identifier, identifier_size) ==
        XIAOTAI_CALL_OUTBOUND_ACCEPTED;
}

int xiaotai_call_encode_device_info(char *output, size_t capacity,
                                    const char *peer, const char *room_id)
{
    if (output == NULL || peer == NULL || room_id == NULL) return -1;
    int length = snprintf(output, capacity,
        "{\"device_id\":\"%s\",\"room_id\":\"%s\","
        "\"purpose\":\"call\"}", peer, room_id);
    return length > 0 && (size_t)length < capacity ? length : -1;
}

int xiaotai_call_encode_device_dial(char *output, size_t capacity,
                                    const char *peer)
{
    if (output == NULL || peer == NULL) return -1;
    int length = snprintf(output, capacity,
        "{\"targets\":[\"%s\"],\"call_type\":\"audio\"}", peer);
    return length > 0 && (size_t)length < capacity ? length : -1;
}

int xiaotai_call_encode_voip_dial(char *output, size_t capacity,
                                  const char *device_id,
                                  const char *openid,
                                  const char *model_id,
                                  const char *app_id)
{
    if (output == NULL || device_id == NULL || device_id[0] == '\0' ||
        openid == NULL || openid[0] == '\0' || model_id == NULL ||
        model_id[0] == '\0') return -1;
    int length = app_id != NULL && app_id[0] != '\0' ?
        snprintf(output, capacity,
            "{\"device_id\":\"%s\",\"wx_app_id\":\"%s\","
            "\"wx_user_openid\":\"%s\",\"wx_model_id\":\"%s\","
            "\"wx_room_type\":\"voice\",\"wx_version_type\":0,"
            "\"calling_timeout_sec\":30,\"wx_caller_camera_status\":1,"
            "\"wx_listener_camera_status\":1}",
            device_id, app_id, openid, model_id) :
        snprintf(output, capacity,
            "{\"device_id\":\"%s\",\"wx_user_openid\":\"%s\","
            "\"wx_model_id\":\"%s\",\"wx_room_type\":\"voice\","
            "\"wx_version_type\":0,\"calling_timeout_sec\":30,"
            "\"wx_caller_camera_status\":1,"
            "\"wx_listener_camera_status\":1}",
            device_id, openid, model_id);
    return length > 0 && (size_t)length < capacity ? length : -1;
}

int xiaotai_call_encode_device_end(char *output, size_t capacity,
                                   const char *room_id, const char *reason,
                                   bool cancel)
{
    if (output == NULL || room_id == NULL || room_id[0] == '\0') return -1;
    int length = cancel ?
        snprintf(output, capacity, "{\"room_id\":\"%s\"}", room_id) :
        snprintf(output, capacity,
                 "{\"room_id\":\"%s\",\"reason\":\"%s\"}",
                 room_id, reason == NULL ? "hangup" : reason);
    return length > 0 && (size_t)length < capacity ? length : -1;
}

char *xiaotai_call_encode_voip_reject(const char *app_id,
                                      const char *model_id,
                                      const char *server_token,
                                      const char *room_id,
                                      const char *payload, int reason)
{
    if (app_id == NULL || app_id[0] == '\0' || model_id == NULL ||
        model_id[0] == '\0' || server_token == NULL ||
        server_token[0] == '\0' || room_id == NULL || room_id[0] == '\0') {
        return NULL;
    }
    cJSON *request = cJSON_CreateObject();
    if (request == NULL ||
        cJSON_AddStringToObject(request, "wx_app_id", app_id) == NULL ||
        cJSON_AddStringToObject(request, "wx_model_id", model_id) == NULL ||
        cJSON_AddStringToObject(request, "wx_session_token", server_token) == NULL ||
        cJSON_AddStringToObject(request, "wx_room_id", room_id) == NULL ||
        cJSON_AddStringToObject(request, "wx_payload",
                               payload == NULL ? "" : payload) == NULL ||
        cJSON_AddNumberToObject(request, "hangup_reason", reason) == NULL) {
        cJSON_Delete(request);
        return NULL;
    }
    char *json = cJSON_PrintUnformatted(request);
    cJSON_Delete(request);
    return json;
}
