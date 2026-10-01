#include "xiaotai_contacts.h"

#include <string.h>

#include "cJSON.h"

static const char *json_string(const cJSON *object, const char *name)
{
    const cJSON *item = cJSON_IsObject(object) ?
        cJSON_GetObjectItemCaseSensitive(object, name) : NULL;
    return cJSON_IsString(item) && item->valuestring != NULL ?
        item->valuestring : "";
}

static const char *json_string_any(const cJSON *object,
                                   const char *first, const char *second,
                                   const char *third, const char *fourth)
{
    const char *value = json_string(object, first);
    if (value[0] == '\0' && second != NULL) value = json_string(object, second);
    if (value[0] == '\0' && third != NULL) value = json_string(object, third);
    if (value[0] == '\0' && fourth != NULL) value = json_string(object, fourth);
    return value;
}

static bool copy_text(char *output, size_t capacity, const char *input)
{
    size_t length = input == NULL ? 0U : strlen(input);
    if (length == 0U || length >= capacity) return false;
    memcpy(output, input, length + 1U);
    return true;
}

static bool normalize_name(const char *input, char *output, size_t capacity)
{
    if (input == NULL || output == NULL || capacity == 0U) return false;
    size_t used = 0U;
    for (; *input != '\0'; ++input) {
        unsigned char value = (unsigned char)*input;
        if (value == ' ' || value == '\t' || value == '\r' || value == '\n' ||
            value == '\v' || value == '\f') {
            continue;
        }
        if (used + 1U >= capacity) return false;
        if (value >= 'A' && value <= 'Z') value = (unsigned char)(value + 32U);
        output[used++] = (char)value;
    }
    output[used] = '\0';
    return used > 0U;
}

static char ascii_lower(char ch)
{
    return ch >= 'A' && ch <= 'Z' ? (char)(ch - 'A' + 'a') : ch;
}

static bool ascii_equal_ignore_case(const char *lhs, const char *rhs)
{
    if (lhs == NULL || rhs == NULL) return false;
    while (*lhs != '\0' && *rhs != '\0') {
        if (ascii_lower(*lhs) != ascii_lower(*rhs)) return false;
        ++lhs;
        ++rhs;
    }
    return *lhs == '\0' && *rhs == '\0';
}

static int channel_type(const char *channel)
{
    if (channel == NULL || channel[0] == '\0') return 0;
    if (ascii_equal_ignore_case(channel, "device") ||
        ascii_equal_ignore_case(channel, "call") ||
        ascii_equal_ignore_case(channel, "device_call") ||
        ascii_equal_ignore_case(channel, "tirtc") ||
        strcmp(channel, "设备") == 0 || strcmp(channel, "设备联系人") == 0) {
        return XIAOTAI_CONTACT_DEVICE;
    }
    if (ascii_equal_ignore_case(channel, "wx") ||
        ascii_equal_ignore_case(channel, "wechat") ||
        ascii_equal_ignore_case(channel, "wechat_voip") ||
        ascii_equal_ignore_case(channel, "voip") ||
        strcmp(channel, "微信") == 0 || strcmp(channel, "微信联系人") == 0) {
        return XIAOTAI_CONTACT_VOIP;
    }
    return -1;
}

static bool call_action_supported(const char *action)
{
    static const char *const actions[] = {
        "call_device", "device_call", "start_device_call", "call_contact",
        "call", "call_wechat", "wechat_call", "call_wechat_contact",
        "call_voip_contact", "voip_call",
    };
    for (size_t i = 0U; i < sizeof(actions) / sizeof(actions[0]); ++i) {
        if (ascii_equal_ignore_case(action, actions[i])) return true;
    }
    return false;
}

static bool wechat_call_action(const char *action)
{
    return ascii_equal_ignore_case(action, "call_wechat") ||
           ascii_equal_ignore_case(action, "wechat_call") ||
           ascii_equal_ignore_case(action, "call_wechat_contact") ||
           ascii_equal_ignore_case(action, "call_voip_contact") ||
           ascii_equal_ignore_case(action, "voip_call");
}

static const cJSON *call_action_payload(const cJSON *params)
{
    static const char *const names[] = {
        "data", "arguments", "args", "input", "payload", "parameters",
    };
    if (!cJSON_IsObject(params)) return NULL;
    for (size_t i = 0U; i < sizeof(names) / sizeof(names[0]); ++i) {
        const cJSON *value =
            cJSON_GetObjectItemCaseSensitive(params, names[i]);
        if (cJSON_IsObject(value)) return value;
    }
    return params;
}

static const char *json_string_from_payload(const cJSON *params,
                                            const cJSON *payload,
                                            const char *const *names,
                                            size_t count)
{
    for (size_t i = 0U; i < count; ++i) {
        const char *value = json_string(payload, names[i]);
        if (value[0] != '\0') return value;
        value = json_string(params, names[i]);
        if (value[0] != '\0') return value;
    }
    return "";
}

void xiaotai_contacts_init(xiaotai_contacts_t *contacts)
{
    if (contacts != NULL) memset(contacts, 0, sizeof(*contacts));
}

xiaotai_contact_match_t xiaotai_contacts_first_of_type(
    const xiaotai_contacts_t *contacts, xiaotai_contact_type_t type,
    xiaotai_contact_t *result)
{
    if (contacts == NULL || result == NULL ||
        (type != XIAOTAI_CONTACT_DEVICE && type != XIAOTAI_CONTACT_VOIP)) {
        return XIAOTAI_CONTACT_MATCH_INVALID;
    }
    for (size_t i = 0U; i < contacts->count; ++i) {
        if (contacts->entries[i].type == type) {
            *result = contacts->entries[i];
            return XIAOTAI_CONTACT_MATCH_OK;
        }
    }
    return XIAOTAI_CONTACT_MATCH_NOT_FOUND;
}

size_t xiaotai_contacts_grouped_indices(const xiaotai_contacts_t *contacts,
                                        size_t *indices, size_t capacity)
{
    if (contacts == NULL || indices == NULL || capacity == 0U) return 0U;
    const xiaotai_contact_type_t groups[] = {
        XIAOTAI_CONTACT_DEVICE, XIAOTAI_CONTACT_VOIP
    };
    size_t used = 0U;
    for (size_t group = 0U; group < sizeof(groups) / sizeof(groups[0]);
         ++group) {
        for (size_t i = 0U; i < contacts->count && used < capacity; ++i) {
            if (contacts->entries[i].type == groups[group]) {
                indices[used++] = i;
            }
        }
    }
    return used;
}

static bool response_array(const char *json, bool voip,
                           cJSON **root_out, const cJSON **array_out)
{
    if (json == NULL || root_out == NULL || array_out == NULL) return false;
    cJSON *root = cJSON_Parse(json);
    const cJSON *code = root == NULL ? NULL :
        cJSON_GetObjectItemCaseSensitive(root, "code");
    const cJSON *data = root == NULL ? NULL :
        cJSON_GetObjectItemCaseSensitive(root, "data");
    const cJSON *array = cJSON_IsObject(data) ?
        cJSON_GetObjectItemCaseSensitive(data, "contacts") : NULL;
    if (voip && !cJSON_IsArray(array) && cJSON_IsObject(data)) {
        array = cJSON_GetObjectItemCaseSensitive(data, "list");
    }
    if (!cJSON_IsNumber(code) ||
        (code->valueint != 0 && code->valueint != 200) ||
        !cJSON_IsArray(array)) {
        cJSON_Delete(root);
        return false;
    }
    *root_out = root;
    *array_out = array;
    return true;
}

static size_t remove_channel(xiaotai_contacts_t *contacts,
                             xiaotai_contact_type_t replaced_type)
{
    size_t kept = 0U;
    for (size_t i = 0; i < contacts->count; ++i) {
        if (contacts->entries[i].type != replaced_type) {
            if (kept != i) contacts->entries[kept] = contacts->entries[i];
            ++kept;
        }
    }
    contacts->count = kept;
    return kept;
}

int xiaotai_contacts_replace_all_json(xiaotai_contacts_t *contacts,
                                      const char *json)
{
    if (contacts == NULL) return -1;
    cJSON *root = NULL;
    const cJSON *array = NULL;
    if (!response_array(json, false, &root, &array)) return -1;

    /* This endpoint is authoritative for both channels. Parse in platform
     * order so contacts[0] remains the platform-selected quick contact. */
    xiaotai_contacts_init(contacts);
    const cJSON *item = NULL;
    cJSON_ArrayForEach(item, array) {
        const char *type = json_string(item, "type");
        const char *id = json_string(item, "device_id");
        if (type[0] == '\0' || id[0] == '\0' ||
            contacts->count >= XIAOTAI_CONTACTS_MAX) {
            continue;
        }

        xiaotai_contact_t value = {0};
        value.type = strcmp(type, "voip") == 0 ?
            XIAOTAI_CONTACT_VOIP : XIAOTAI_CONTACT_DEVICE;
        const char *name = json_string_any(item, "remark", "device_name",
                                           "name", NULL);
        if (name[0] == '\0') name = id;
        if (!copy_text(value.id, sizeof(value.id), id) ||
            !copy_text(value.name, sizeof(value.name), name)) {
            continue;
        }

        if (value.type == XIAOTAI_CONTACT_DEVICE) {
            const cJSON *online =
                cJSON_GetObjectItemCaseSensitive(item, "online");
            value.online = cJSON_IsTrue(online);
        } else {
            const char *model_id = json_string_any(
                item, "wx_model_id", "wxa_model_id", NULL, NULL);
            const char *app_id = json_string_any(
                item, "wx_app_id", "wxa_app_id", NULL, NULL);
            if (model_id[0] != '\0') {
                (void)copy_text(value.model_id, sizeof(value.model_id),
                                model_id);
            }
            if (app_id[0] != '\0') {
                (void)copy_text(value.app_id, sizeof(value.app_id), app_id);
            }
            value.online = true;
        }
        contacts->entries[contacts->count++] = value;
    }
    cJSON_Delete(root);
    return (int)contacts->count;
}

int xiaotai_contacts_replace_device_json(xiaotai_contacts_t *contacts,
                                         const char *json)
{
    if (contacts == NULL) return -1;
    cJSON *root = NULL;
    const cJSON *array = NULL;
    if (!response_array(json, false, &root, &array)) return -1;

    /* The response envelope is now known to be valid, so replace this
     * channel directly in the cache. Keeping a temporary contacts table on
     * this 6 KB task stack overflowed it on the first 200 response. */
    (void)remove_channel(contacts, XIAOTAI_CONTACT_DEVICE);
    int added = 0;
    const cJSON *item = NULL;
    cJSON_ArrayForEach(item, array) {
        const char *type = json_string(item, "type");
        const char *id = json_string(item, "device_id");
        const char *name = json_string(item, "remark");
        if (name[0] == '\0' || id[0] == '\0' ||
            (type[0] != '\0' && strcmp(type, "device") != 0) ||
            contacts->count >= XIAOTAI_CONTACTS_MAX) continue;
        xiaotai_contact_t *entry = &contacts->entries[contacts->count];
        memset(entry, 0, sizeof(*entry));
        if (!copy_text(entry->id, sizeof(entry->id), id) ||
            !copy_text(entry->name, sizeof(entry->name), name)) {
            continue;
        }
        entry->type = XIAOTAI_CONTACT_DEVICE;
        const cJSON *online = cJSON_GetObjectItemCaseSensitive(item, "online");
        entry->online = cJSON_IsTrue(online);
        contacts->count++;
        added++;
    }
    cJSON_Delete(root);
    return added;
}

int xiaotai_contacts_replace_voip_json(xiaotai_contacts_t *contacts,
                                       const char *json)
{
    if (contacts == NULL) return -1;
    cJSON *root = NULL;
    const cJSON *array = NULL;
    if (!response_array(json, true, &root, &array)) return -1;

    (void)remove_channel(contacts, XIAOTAI_CONTACT_VOIP);
    int added = 0;
    const cJSON *item = NULL;
    cJSON_ArrayForEach(item, array) {
        const char *id = json_string_any(item, "wx_open_id", "wxa_open_id",
                                         "wx_user_openid", "wxa_user_openid");
        const char *model_id = json_string_any(item, "wx_model_id",
                                               "wxa_model_id", NULL, NULL);
        const char *app_id = json_string_any(item, "wx_app_id", "wxa_app_id",
                                             NULL, NULL);
        const char *name = json_string_any(item, "remark", "alias",
                                           "contact_name", "nickname");
        if (id[0] == '\0' || model_id[0] == '\0' || name[0] == '\0' ||
            contacts->count >= XIAOTAI_CONTACTS_MAX) continue;
        xiaotai_contact_t *entry = &contacts->entries[contacts->count];
        memset(entry, 0, sizeof(*entry));
        if (!copy_text(entry->id, sizeof(entry->id), id) ||
            !copy_text(entry->model_id, sizeof(entry->model_id), model_id) ||
            !copy_text(entry->name, sizeof(entry->name), name) ||
            (app_id[0] != '\0' &&
             !copy_text(entry->app_id, sizeof(entry->app_id), app_id))) {
            continue;
        }
        entry->type = XIAOTAI_CONTACT_VOIP;
        entry->online = true;
        contacts->count++;
        added++;
    }
    cJSON_Delete(root);
    return added;
}

xiaotai_contact_match_t xiaotai_contacts_match(
    const xiaotai_contacts_t *contacts, const char *name,
    const char *target_id, const char *channel, xiaotai_contact_t *result)
{
    if (contacts == NULL || result == NULL) return XIAOTAI_CONTACT_MATCH_INVALID;
    char wanted[XIAOTAI_CONTACT_NAME_MAX + 1U];
    int wanted_type = channel_type(channel);
    if (!normalize_name(name, wanted, sizeof(wanted)) || wanted_type < 0) {
        return XIAOTAI_CONTACT_MATCH_INVALID;
    }

    size_t matches = 0U;
    xiaotai_contact_t found = {0};
    for (size_t i = 0; i < contacts->count; ++i) {
        const xiaotai_contact_t *entry = &contacts->entries[i];
        char normalized[XIAOTAI_CONTACT_NAME_MAX + 1U];
        if ((wanted_type != 0 && (int)entry->type != wanted_type) ||
            !normalize_name(entry->name, normalized, sizeof(normalized)) ||
            strcmp(wanted, normalized) != 0) {
            continue;
        }
        matches++;
        found = *entry;
    }
    if (matches == 0U) return XIAOTAI_CONTACT_MATCH_NOT_FOUND;
    if (matches != 1U) return XIAOTAI_CONTACT_MATCH_AMBIGUOUS;
    if (target_id != NULL && target_id[0] != '\0' &&
        strcmp(target_id, found.id) != 0) {
        return XIAOTAI_CONTACT_MATCH_NOT_FOUND;
    }
    *result = found;
    return XIAOTAI_CONTACT_MATCH_OK;
}

xiaotai_contact_match_t xiaotai_contacts_first(
    const xiaotai_contacts_t *contacts, xiaotai_contact_t *result)
{
    if (contacts == NULL || result == NULL) {
        return XIAOTAI_CONTACT_MATCH_INVALID;
    }
    if (contacts->count == 0U) return XIAOTAI_CONTACT_MATCH_NOT_FOUND;
    *result = contacts->entries[0];
    return XIAOTAI_CONTACT_MATCH_OK;
}

xiaotai_contact_match_t xiaotai_contacts_resolve_call_intent_json(
    const xiaotai_contacts_t *contacts, const char *json,
    xiaotai_contact_t *result)
{
    if (contacts == NULL || json == NULL || result == NULL) {
        return XIAOTAI_CONTACT_MATCH_INVALID;
    }
    cJSON *root = cJSON_Parse(json);
    const cJSON *params = root == NULL ? NULL :
        cJSON_GetObjectItemCaseSensitive(root, "params");
    const char *method = json_string(root, "method");
    const cJSON *payload = call_action_payload(params);
    static const char *const name_fields[] = {
        "target_name", "target", "target_device", "device_name",
        "device_alias", "contact", "contact_name", "name", "alias",
        "remark", "callee", "peer", "nickname",
    };
    static const char *const id_fields[] = {
        "target_id", "target_device_id", "device_id", "callee_device_id",
        "peer_id",
    };
    static const char *const channel_fields[] = {
        "channel", "contact_type", "target_type", "contact_source", "route",
    };
    static const char *const media_fields[] = {
        "media", "call_type", "type", "mode",
    };
    const char *name = json_string_from_payload(
        params, payload, name_fields, sizeof(name_fields) / sizeof(name_fields[0]));
    const char *target_id = json_string_from_payload(
        params, payload, id_fields, sizeof(id_fields) / sizeof(id_fields[0]));
    const char *channel = json_string_from_payload(
        params, payload, channel_fields,
        sizeof(channel_fields) / sizeof(channel_fields[0]));
    const char *media = json_string_from_payload(
        params, payload, media_fields,
        sizeof(media_fields) / sizeof(media_fields[0]));

    const char *action = json_string(params, "action");
    if (action[0] == '\0') action = json_string(params, "name");
    if (action[0] == '\0') action = json_string(params, "tool");
    if (action[0] == '\0') action = json_string(params, "function");
    if (action[0] == '\0') action = json_string(payload, "action");
    if (action[0] == '\0') action = json_string(payload, "tool");
    if (action[0] == '\0') action = json_string(payload, "function");
    bool device_action = strcmp(method, "device_action") == 0;
    if (device_action && !call_action_supported(action)) {
        cJSON_Delete(root);
        return XIAOTAI_CONTACT_MATCH_INVALID;
    }
    if (channel[0] == '\0' && device_action && wechat_call_action(action)) {
        channel = "wechat";
    } else if (channel[0] == '\0' &&
               (channel_type(media) == XIAOTAI_CONTACT_DEVICE ||
                channel_type(media) == XIAOTAI_CONTACT_VOIP)) {
        channel = media;
    }
    bool audio = media[0] == '\0' ||
                 ascii_equal_ignore_case(media, "audio") ||
                 ascii_equal_ignore_case(media, "voice") ||
                 channel_type(media) > 0;
    xiaotai_contact_match_t match =
        !audio ?
        XIAOTAI_CONTACT_MATCH_INVALID :
        xiaotai_contacts_match(contacts, name, target_id, channel, result);
    cJSON_Delete(root);
    return match;
}
