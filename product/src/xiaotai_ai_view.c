#include "xiaotai_ai_view.h"

#include <stddef.h>
#include <stdio.h>
#include <string.h>

#include "cJSON.h"

typedef struct {
    const char *tag;
    const char *label_zh;
} emotion_entry_t;

typedef struct {
    const char *platform_value;
    const char *tag;
} emotion_alias_t;

static const emotion_entry_t s_emotions[] = {
    {"neutral", "平静"},
    {"happy", "开心"},
    {"laughing", "大笑"},
    {"funny", "逗趣"},
    {"sad", "难过"},
    {"angry", "生气"},
    {"crying", "哭泣"},
    {"loving", "喜爱"},
    {"embarrassed", "害羞"},
    {"surprised", "惊讶"},
    {"shocked", "震惊"},
    {"thinking", "思考"},
    {"winking", "眨眼"},
    {"cool", "酷"},
    {"relaxed", "放松"},
    {"delicious", "美味"},
    {"kissy", "亲亲"},
    {"confident", "自信"},
    {"sleepy", "困倦"},
    {"silly", "搞怪"},
    {"confused", "困惑"},
};

/* The AI event protocol defines Chinese sentence-level emotion values. Keep
 * the renderer keys stable and translate at the product boundary. */
static const emotion_alias_t s_emotion_aliases[] = {
    {"中性", "neutral"},
    {"开心", "happy"},
    {"兴奋", "laughing"},
    {"温和", "relaxed"},
    {"安慰", "loving"},
    {"思考", "thinking"},
    {"惊讶", "surprised"},
    {"严肃", "confident"},
    {"困倦", "sleepy"},
    {"困惑", "confused"},
};

static const char *json_string(const cJSON *object, const char *name)
{
    const cJSON *item = cJSON_IsObject(object) ?
        cJSON_GetObjectItemCaseSensitive(object, name) : NULL;
    return cJSON_IsString(item) && item->valuestring != NULL ?
        item->valuestring : "";
}

static const char *json_emotion(const cJSON *object)
{
    static const char *const names[] = {
        "emotion", "emotion_tag", "expression", "tag",
    };
    if (!cJSON_IsObject(object)) return "";
    for (size_t i = 0U; i < sizeof(names) / sizeof(names[0]); ++i) {
        const char *value = json_string(object, names[i]);
        if (value[0] != '\0') return value;
    }
    const cJSON *data = cJSON_GetObjectItemCaseSensitive(object, "data");
    if (cJSON_IsObject(data)) {
        for (size_t i = 0U; i < sizeof(names) / sizeof(names[0]); ++i) {
            const char *value = json_string(data, names[i]);
            if (value[0] != '\0') return value;
        }
    }
    return "";
}

size_t xiaotai_ai_emotion_count(void)
{
    return sizeof(s_emotions) / sizeof(s_emotions[0]);
}

const char *xiaotai_ai_emotion_at(size_t index)
{
    return index < xiaotai_ai_emotion_count() ? s_emotions[index].tag : NULL;
}

static const emotion_entry_t *emotion_entry(const char *emotion)
{
    if (emotion != NULL) {
        for (size_t i = 0U; i < xiaotai_ai_emotion_count(); ++i) {
            if (strcmp(emotion, s_emotions[i].tag) == 0) {
                return &s_emotions[i];
            }
        }
        for (size_t i = 0U;
             i < sizeof(s_emotion_aliases) / sizeof(s_emotion_aliases[0]);
             ++i) {
            if (strcmp(emotion, s_emotion_aliases[i].platform_value) != 0) {
                continue;
            }
            for (size_t j = 0U; j < xiaotai_ai_emotion_count(); ++j) {
                if (strcmp(s_emotion_aliases[i].tag,
                           s_emotions[j].tag) == 0) {
                    return &s_emotions[j];
                }
            }
        }
    }
    return NULL;
}

const char *xiaotai_ai_emotion_label_zh(const char *emotion)
{
    const emotion_entry_t *entry = emotion_entry(emotion);
    return entry != NULL ? entry->label_zh : "开心";
}

static void caption_copy(char *destination, size_t capacity,
                         const char *source, bool append)
{
    if (capacity == 0U) return;
    size_t used = append ? strlen(destination) : 0U;
    if (!append) destination[0] = '\0';
    if (source == NULL || used >= capacity - 1U) return;
    size_t available = capacity - used - 1U;
    size_t source_length = strlen(source);
    size_t length = source_length > available ? available : source_length;
    memcpy(destination + used, source, length);
    destination[used + length] = '\0';
    if (length < source_length) {
        while (length > 0U &&
               (((unsigned char)destination[used + length - 1U] & 0xc0U) ==
                0x80U)) {
            destination[used + --length] = '\0';
        }
        if (length > 0U &&
            (unsigned char)destination[used + length - 1U] >= 0xc0U) {
            destination[used + length - 1U] = '\0';
        }
    }
}

void xiaotai_ai_view_init(xiaotai_ai_view_t *view)
{
    if (view == NULL) return;
    memset(view, 0, sizeof(*view));
    view->phase = XIAOTAI_AI_UI_LISTENING;
    view->caption_type = -1;
    snprintf(view->emotion, sizeof(view->emotion), "happy");
}

void xiaotai_ai_view_set_emotion(xiaotai_ai_view_t *view,
                                 const char *emotion)
{
    if (view == NULL) return;
    const emotion_entry_t *entry = emotion_entry(emotion);
    if (entry != NULL) {
        snprintf(view->emotion, sizeof(view->emotion), "%s", entry->tag);
    }
}

bool xiaotai_ai_view_apply(xiaotai_ai_view_t *view, const char *json,
                           uint32_t now_ms)
{
    if (view == NULL || json == NULL) return false;
    cJSON *root = cJSON_Parse(json);
    const cJSON *method = root == NULL ? NULL :
        cJSON_GetObjectItemCaseSensitive(root, "method");
    const cJSON *params = root == NULL ? NULL :
        cJSON_GetObjectItemCaseSensitive(root, "params");
    const cJSON *legacy_type = root == NULL ? NULL :
        cJSON_GetObjectItemCaseSensitive(root, "type");
    bool legacy_reply = cJSON_IsString(legacy_type) &&
        legacy_type->valuestring != NULL &&
        (strcmp(legacy_type->valuestring, "ai_reply") == 0 ||
         strcmp(legacy_type->valuestring, "AI_EMOTION") == 0 ||
         strcmp(legacy_type->valuestring, "ai_emotion") == 0);
    if ((!cJSON_IsString(method) || method->valuestring == NULL) &&
        !legacy_reply) {
        cJSON_Delete(root);
        return false;
    }
    bool present = true;
    if (legacy_reply) {
        const char *emotion = json_emotion(root);
        const char *text = json_string(root, "text");
        if (emotion[0] != '\0') xiaotai_ai_view_set_emotion(view, emotion);
        if (text[0] != '\0') {
            caption_copy(view->caption, sizeof(view->caption), text, false);
            view->caption_type = 1;
            view->phase = XIAOTAI_AI_UI_SPEAKING;
        }
    } else if (strcmp(method->valuestring, "caption") == 0 &&
               cJSON_IsObject(params)) {
        const cJSON *caption_type = cJSON_GetObjectItemCaseSensitive(
            params, "caption_type");
        const cJSON *value = cJSON_GetObjectItemCaseSensitive(params, "text");
        const cJSON *mode = cJSON_GetObjectItemCaseSensitive(params, "mode");
        const cJSON *final = cJSON_GetObjectItemCaseSensitive(params,
                                                               "is_final");
        const char *utterance_id = json_string(params, "utterance_id");
        if (!cJSON_IsNumber(caption_type) || !cJSON_IsString(value) ||
            value->valuestring == NULL) {
            cJSON_Delete(root);
            return false;
        }
        int type = caption_type->valueint;
        bool same_group = type == view->caption_type &&
                          strcmp(utterance_id, view->utterance_id) == 0;
        bool append = same_group && cJSON_IsNumber(mode) &&
                      mode->valueint == 1;
        caption_copy(view->caption, sizeof(view->caption),
                     value->valuestring, append);
        view->caption_type = type;
        snprintf(view->utterance_id, sizeof(view->utterance_id), "%s",
                 utterance_id);
        const char *emotion = json_emotion(params);
        if (emotion[0] != '\0') xiaotai_ai_view_set_emotion(view, emotion);
        bool is_final = cJSON_IsBool(final) && cJSON_IsTrue(final);
        view->phase = type == 1 ? XIAOTAI_AI_UI_SPEAKING :
            (is_final ? XIAOTAI_AI_UI_THINKING : XIAOTAI_AI_UI_LISTENING);
        present = is_final || now_ms - view->last_present_ms >= 100U;
    } else if (strcmp(method->valuestring, "round_start") == 0) {
        const char *emotion = json_emotion(params);
        if (emotion[0] != '\0') xiaotai_ai_view_set_emotion(view, emotion);
        view->phase = XIAOTAI_AI_UI_SPEAKING;
    } else if (strcmp(method->valuestring, "round_end") == 0) {
        view->phase = XIAOTAI_AI_UI_LISTENING;
    } else if (strcmp(method->valuestring, "emotion") == 0 ||
               strcmp(method->valuestring, "ai_emotion") == 0 ||
               strcmp(method->valuestring, "expression") == 0 ||
               strcmp(method->valuestring, "event") == 0) {
        const char *emotion = json_emotion(params);
        if (emotion[0] == '\0') {
            present = false;
        } else {
            xiaotai_ai_view_set_emotion(view, emotion);
        }
    } else {
        present = false;
    }
    cJSON_Delete(root);
    if (present) view->last_present_ms = now_ms;
    return present;
}
