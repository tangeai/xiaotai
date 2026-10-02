#include <assert.h>
#include <string.h>

#include "xiaotai_ai_view.h"

int main(void)
{
    static const char *const emotions[] = {
        "neutral", "happy", "laughing", "funny", "sad", "angry",
        "crying", "loving", "embarrassed", "surprised", "shocked",
        "thinking", "winking", "cool", "relaxed", "delicious", "kissy",
        "confident", "sleepy", "silly", "confused",
    };
    static const char *const labels[] = {
        "平静", "开心", "大笑", "逗趣", "难过", "生气", "哭泣",
        "喜爱", "害羞", "惊讶", "震惊", "思考", "眨眼", "酷",
        "放松", "美味", "亲亲", "自信", "困倦", "搞怪", "困惑",
    };
    assert(xiaotai_ai_emotion_count() == 21U);
    for (size_t i = 0U; i < xiaotai_ai_emotion_count(); ++i) {
        assert(strcmp(xiaotai_ai_emotion_at(i), emotions[i]) == 0);
        assert(strcmp(xiaotai_ai_emotion_label_zh(emotions[i]), labels[i]) == 0);
    }
    assert(xiaotai_ai_emotion_at(21U) == NULL);
    static const char *const platform_emotions[] = {
        "中性", "开心", "兴奋", "温和", "安慰", "思考", "惊讶",
        "严肃", "困倦", "困惑",
    };
    static const char *const mapped_emotions[] = {
        "neutral", "happy", "laughing", "relaxed", "loving", "thinking",
        "surprised", "confident", "sleepy", "confused",
    };

    xiaotai_ai_view_t view;
    xiaotai_ai_view_init(&view);
    assert(view.phase == XIAOTAI_AI_UI_LISTENING);
    assert(strcmp(view.emotion, "happy") == 0);
    for (size_t i = 0U;
         i < sizeof(platform_emotions) / sizeof(platform_emotions[0]); ++i) {
        xiaotai_ai_view_set_emotion(&view, platform_emotions[i]);
        assert(strcmp(view.emotion, mapped_emotions[i]) == 0);
    }

    assert(xiaotai_ai_view_apply(&view,
        "{\"method\":\"caption\",\"params\":{\"caption_type\":0,"
        "\"utterance_id\":\"u1\",\"text\":\"你好\",\"mode\":0,"
        "\"is_final\":true,\"emotion\":\"sad\"}}", 100U));
    assert(view.phase == XIAOTAI_AI_UI_THINKING);
    assert(strcmp(view.caption, "你好") == 0);
    assert(strcmp(view.emotion, "sad") == 0);

    assert(xiaotai_ai_view_apply(&view,
        "{\"method\":\"emotion\",\"params\":{\"tag\":\"angry\"}}", 101U));
    assert(strcmp(view.emotion, "angry") == 0);
    assert(xiaotai_ai_view_apply(&view,
        "{\"method\":\"round_start\",\"params\":{\"emotion\":\"cool\"}}",
        102U));
    assert(strcmp(view.emotion, "cool") == 0);
    assert(xiaotai_ai_view_apply(&view,
        "{\"type\":\"ai_reply\",\"text\":\"真棒\","
        "\"emotion\":\"surprised\",\"is_final\":true}", 103U));
    assert(strcmp(view.caption, "真棒") == 0);
    assert(strcmp(view.emotion, "surprised") == 0);
    assert(xiaotai_ai_view_apply(&view,
        "{\"method\":\"event\",\"params\":{\"type\":\"emotion\","
        "\"data\":{\"emotion\":\"crying\"}}}", 104U));
    assert(strcmp(view.emotion, "crying") == 0);
    assert(xiaotai_ai_view_apply(&view,
        "{\"method\":\"event\",\"params\":{\"data\":{\"tag\":\"confused\"}}}",
        105U));
    assert(strcmp(view.emotion, "confused") == 0);
    assert(xiaotai_ai_view_apply(&view,
        "{\"method\":\"caption\",\"params\":{\"caption_type\":1,"
        "\"utterance_id\":\"u3\",\"text\":\"让我想想\",\"mode\":0,"
        "\"is_final\":true,\"emotion\":\"thinking\"}}", 106U));
    assert(strcmp(view.emotion, "thinking") == 0);

    assert(!xiaotai_ai_view_apply(&view,
        "{\"method\":\"caption\",\"params\":{\"caption_type\":1,"
        "\"utterance_id\":\"u2\",\"text\":\"A\",\"mode\":0}}", 150U));
    assert(view.phase == XIAOTAI_AI_UI_SPEAKING);
    assert(xiaotai_ai_view_apply(&view, "{\"method\":\"round_end\"}", 151U));
    assert(view.phase == XIAOTAI_AI_UI_LISTENING);

    for (size_t i = 0U; i < xiaotai_ai_emotion_count(); ++i) {
        xiaotai_ai_view_set_emotion(&view, emotions[i]);
        assert(strcmp(view.emotion, emotions[i]) == 0);
    }
    xiaotai_ai_view_set_emotion(&view, "sad");
    xiaotai_ai_view_set_emotion(&view, "unknown");
    assert(strcmp(view.emotion, "sad") == 0);
    assert(strcmp(xiaotai_ai_emotion_label_zh("unknown"), "开心") == 0);
    assert(xiaotai_ai_view_apply(&view,
        "{\"method\":\"caption\",\"params\":{\"caption_type\":1,"
        "\"utterance_id\":\"u4\",\"text\":\"没有情绪字段\",\"mode\":0,"
        "\"is_final\":true}}", 300U));
    assert(strcmp(view.emotion, "sad") == 0);
    return 0;
}
