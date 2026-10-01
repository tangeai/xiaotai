#include "xiaotai_signal.h"

#include <string.h>

#include "cJSON.h"

static const char *json_string(const cJSON *object, const char *name)
{
    const cJSON *item = cJSON_IsObject(object) ?
        cJSON_GetObjectItemCaseSensitive(object, name) : NULL;
    return cJSON_IsString(item) && item->valuestring != NULL ?
        item->valuestring : "";
}

bool xiaotai_signal_decode(const char *json, xiaotai_signal_visitor_fn visitor,
                           void *context)
{
    if (json == NULL || visitor == NULL) return false;
    cJSON *root = cJSON_Parse(json);
    const char *name = json_string(root, "type");
    if (!cJSON_IsObject(root) || name[0] == '\0') {
        cJSON_Delete(root);
        return false;
    }
    const char *channel = json_string(root, "channel");
    const cJSON *payload = cJSON_GetObjectItemCaseSensitive(root, "payload");
    xiaotai_signal_view_t signal = {
        .type = XIAOTAI_SIGNAL_UNKNOWN,
        .name = name,
        .room_id = json_string(payload, "room_id"),
        .caller_id = json_string(payload, "caller_id"),
        .call_type = json_string(payload, "call_type"),
        .peer_id = json_string(payload, "peer_id"),
        .token = json_string(payload, "token"),
        .wx_room_id = json_string(payload, "wx_room_id"),
        .wx_call_id = json_string(payload, "wx_call_id"),
        .wx_user_openid = json_string(payload, "wx_user_openid"),
        .wx_from = json_string(payload, "wx_from"),
        .wx_app_id = json_string(payload, "wx_app_id"),
        .wx_model_id = json_string(payload, "wx_model_id"),
        .wx_server_token = json_string(payload, "wx_server_token"),
        .wx_payload = json_string(payload, "wx_payload"),
    };
    if (strcmp(name, "unbind") == 0) {
        signal.type = XIAOTAI_SIGNAL_UNBIND;
    } else if (strcmp(name, "call_incoming") == 0 &&
               strcmp(channel, "device") == 0) {
        signal.type = XIAOTAI_SIGNAL_DEVICE_CALL_INCOMING;
    } else if (strcmp(name, "call_incoming") == 0 &&
               (channel[0] == '\0' || strcmp(channel, "wx") == 0 ||
                strcmp(channel, "voip") == 0)) {
        signal.type = XIAOTAI_SIGNAL_VOIP_CALL_INCOMING;
    } else if (strcmp(name, "callers_update") == 0) {
        signal.type = XIAOTAI_SIGNAL_CONTACTS_CHANGED;
    } else if (strcmp(name, "room_assignment_changed") == 0) {
        signal.type = XIAOTAI_SIGNAL_ROOM_ASSIGNMENT_CHANGED;
    } else if (strcmp(name, "room_closed") == 0) {
        signal.type = XIAOTAI_SIGNAL_ROOM_CLOSED;
    } else if (strcmp(name, "call_cancel") == 0 ||
               strcmp(name, "room_cancel") == 0 ||
               strcmp(name, "call_reject") == 0) {
        signal.type = XIAOTAI_SIGNAL_CALL_ENDED;
    }
    visitor(&signal, context);
    cJSON_Delete(root);
    return true;
}
