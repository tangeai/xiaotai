#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "xiaotai_ai_view.h"
#include "xiaotai_ai_history.h"

static void apply_caption(xiaotai_ai_view_t *view, const char *text, int mode)
{
    char json[2048];
    int size = snprintf(json, sizeof(json),
        "{\"method\":\"caption\",\"params\":{\"caption_type\":1,"
        "\"utterance_id\":\"boundary\",\"text\":\"%s\",\"mode\":%d,"
        "\"is_final\":true}}", text, mode);
    assert(size > 0 && (size_t)size < sizeof(json));
    assert(xiaotai_ai_view_apply(view, json, 100U));
}

/* Use the protocol entry point, including replacement and streamed append.
 * At every byte alignment, retain the longest complete UTF-8 prefix that fits. */
static void test_caption_utf8_capacity(void)
{
    const char *const characters[] = {"A", "é", "你", "🙂"};
    unsigned cases = 0;
    for (unsigned append = 0; append < 2; ++append) {
        for (size_t prefix = 0; prefix <= 255; ++prefix) {
            for (size_t c = 0; c < sizeof(characters) / sizeof(characters[0]); ++c) {
                const size_t width = strlen(characters[c]);
                char text[1024], expected[256], initial[256];
                memset(initial, 'x', prefix);
                initial[prefix] = '\0';
                size_t offset = append ? 0 : prefix;
                memcpy(text, initial, offset);
                for (unsigned i = 0; i < 260U / width + 1U; ++i) {
                    memcpy(text + offset, characters[c], width);
                    offset += width;
                }
                text[offset] = '\0';
                memcpy(expected, initial, prefix);
                size_t used = prefix;
                while (used + width <= 255) {
                    memcpy(expected + used, characters[c], width);
                    used += width;
                }
                expected[used] = '\0';
                xiaotai_ai_view_t view;
                xiaotai_ai_view_init(&view);
                if (append) apply_caption(&view, initial, 0);
                apply_caption(&view, text, (int)append);
                if (strcmp(view.caption, expected) != 0) {
                    fprintf(stderr, "UTF-8 capacity: append=%u prefix=%zu width=%zu expected=%zu actual=%zu\n",
                            append, prefix, width, used, strlen(view.caption));
                    assert(!"caption discarded a complete character or split UTF-8");
                }
                ++cases;
            }
        }
    }
    printf("AI caption UTF-8 boundary cases: %u\n", cases);
}

static void test_history_protocol_sequences(void)
{
    xiaotai_ai_history_t history = {0};
    xiaotai_ai_view_t view;
    xiaotai_ai_view_init(&view);
    for (unsigned round = 0; round < 10000; ++round) {
        uint32_t generation = round / 10U;
        assert(xiaotai_ai_view_apply(&view, "{\"method\":\"round_start\"}", 0));
        xiaotai_ai_history_update(&history, &view, generation);
        apply_caption(&view, "partial", 0);
        xiaotai_ai_history_update(&history, &view, generation);
        char final[32];
        snprintf(final, sizeof(final), "第%u轮", round);
        apply_caption(&view, final, 0);
        xiaotai_ai_history_update(&history, &view, generation);
        assert(xiaotai_ai_view_apply(&view, "{\"method\":\"round_end\"}", 0));
        xiaotai_ai_history_update(&history, &view, generation);
        unsigned count = round < 12U ? round + 1U : 12U;
        assert(history.count == count);
        for (unsigned i = 0; i < count; ++i) {
            snprintf(final, sizeof(final), "第%u轮", round + 1U - count + i);
            assert(!strcmp(history.entries[i].text, final));
            assert(history.entries[i].speaker == 1);
        }
    }
    puts("AI history protocol rounds: 10000");
}

static void test_default_face_is_not_a_server_tag(void)
{
    xiaotai_ai_view_t view;
    xiaotai_ai_view_init(&view);
    assert(!strcmp(view.emotion, "happy") && !view.emotion_tag_present);
    assert(xiaotai_ai_view_apply(&view, "{\"method\":\"round_start\"}", 100U));
    assert(xiaotai_ai_view_apply(&view,
        "{\"method\":\"caption\",\"params\":{\"caption_type\":1,"
        "\"utterance_id\":\"default\",\"text\":\"A\",\"is_final\":true}}", 101U));
    assert(!strcmp(view.emotion, "happy") && !view.emotion_tag_present);
    /* Same rendered name, different ownership: an explicit server happy TAG. */
    assert(xiaotai_ai_view_apply(&view,
        "{\"method\":\"caption\",\"params\":{\"caption_type\":1,"
        "\"utterance_id\":\"tagged\",\"text\":\"B\",\"emotion\":\"开心\","
        "\"is_final\":true}}", 102U));
    assert(!strcmp(view.emotion, "happy") && view.emotion_tag_present);
    /* The next untagged reply restores the default face, not the old TAG. */
    assert(xiaotai_ai_view_apply(&view,
        "{\"method\":\"caption\",\"params\":{\"caption_type\":1,"
        "\"utterance_id\":\"default-again\",\"text\":\"C\",\"is_final\":true}}", 103U));
    assert(!strcmp(view.emotion, "happy") && !view.emotion_tag_present);
}

/* Captured music trace: one tagged final caption followed by untagged finals
 * in the SAME utterance. A completed segment must not lend its TAG forever. */
static void test_music_segments_do_not_inherit_completed_tag(void)
{
    for (unsigned with_id = 0; with_id < 2; ++with_id) {
        xiaotai_ai_view_t view;
        xiaotai_ai_view_init(&view);
        char message[512];
        const char *id = with_id ? "\"utterance_id\":\"same-song\"," : "";
        snprintf(message, sizeof(message),
            "{\"method\":\"caption\",\"params\":{%s\"caption_type\":1,"
            "\"text\":\"intro\",\"emotion\":\"happy\",\"is_final\":true}}", id);
        assert(xiaotai_ai_view_apply(&view, message, 29698));
        assert(view.emotion_tag_present);
        const unsigned times[] = {32444,38234,40353,41950,43197,44786,46753,48274,49446,51020};
        for (unsigned i = 0; i < sizeof(times)/sizeof(times[0]); ++i) {
            snprintf(message, sizeof(message),
                "{\"method\":\"caption\",\"params\":{%s\"caption_type\":1,"
                "\"text\":\"song %u\",\"is_final\":true}}", id, i);
            assert(xiaotai_ai_view_apply(&view, message, times[i]));
            assert(!view.emotion_tag_present && "old intro TAG disables music mouth");
            assert(!strcmp(view.emotion,"happy") && view.phase==XIAOTAI_AI_UI_SPEAKING);
        }
        /* An explicit neutral is still a server TAG, including on music. */
        snprintf(message, sizeof(message),
            "{\"method\":\"caption\",\"params\":{%s\"caption_type\":1,"
            "\"text\":\"tagged music\",\"emotion\":\"neutral\",\"is_final\":true}}", id);
        assert(xiaotai_ai_view_apply(&view, message, 51021));
        assert(view.emotion_tag_present && !strcmp(view.emotion,"neutral"));
        assert(xiaotai_ai_view_apply(&view,"{\"method\":\"round_end\"}",51022));
        assert(view.emotion_tag_present); /* Keep the tagged segment's audio tail. */
        /* A new untagged partial after a completed segment clears immediately,
         * even within the 100 ms UI throttle. */
        snprintf(message, sizeof(message),
            "{\"method\":\"caption\",\"params\":{%s\"caption_type\":1,"
            "\"text\":\"next segment\",\"is_final\":false}}", id);
        assert(xiaotai_ai_view_apply(&view, message, 51023));
        assert(!view.emotion_tag_present);
        /* Partial -> final within one segment retains its explicit TAG. */
        snprintf(message, sizeof(message),
            "{\"method\":\"caption\",\"params\":{%s\"caption_type\":1,"
            "\"text\":\"partial\",\"emotion\":\"neutral\",\"is_final\":false}}", id);
        assert(xiaotai_ai_view_apply(&view, message, 51024));
        snprintf(message, sizeof(message),
            "{\"method\":\"caption\",\"params\":{%s\"caption_type\":1,"
            "\"text\":\"partial complete\",\"is_final\":true}}", id);
        assert(xiaotai_ai_view_apply(&view, message, 51025));
        assert(view.emotion_tag_present && !strcmp(view.emotion,"neutral"));
        /* Standalone emotion applies once even when the previous segment ended
         * in the same group. Malformed messages must not consume that TAG. */
        assert(xiaotai_ai_view_apply(&view,
            "{\"method\":\"emotion\",\"params\":{\"tag\":\"sad\"}}",51026));
        assert(!xiaotai_ai_view_apply(&view,
            "{\"method\":\"caption\",\"params\":{\"caption_type\":1,\"is_final\":false}}",51027));
        assert(view.emotion_tag_pending_caption);
        assert(xiaotai_ai_view_apply(&view, message, 51028));
        assert(view.emotion_tag_present && !strcmp(view.emotion,"sad"));
        assert(!view.emotion_tag_pending_caption);
        assert(xiaotai_ai_view_apply(&view, message, 51029));
        assert(!view.emotion_tag_present);
    }
}

/* Exercise the presentation contract across every supported emotion and TAG
 * source, both caption merge modes, optional IDs and the uint32 clock wrap. */
static void test_emotion_segment_matrix(void)
{
    unsigned cases = 0;
    for (size_t emotion = 0; emotion < xiaotai_ai_emotion_count(); ++emotion) {
        const char *tag = xiaotai_ai_emotion_at(emotion);
        for (unsigned source = 0; source < 3; ++source) {
            for (unsigned options = 0; options < 8; ++options) {
                const unsigned mode = options & 1U;
                const char *id = (options & 2U) ?
                    "\"utterance_id\":\"matrix\"," : "";
                uint32_t now = (options & 4U) ? UINT32_MAX - 2U : 100U;
                xiaotai_ai_view_t view;
                xiaotai_ai_view_init(&view);
                char message[512];
                snprintf(message, sizeof(message),
                    "{\"method\":\"%s\",\"params\":{\"emotion\":\"%s\"}}",
                    source == 1 ? "round_start" : "emotion", tag);
                if (source != 0) {
                    assert(xiaotai_ai_view_apply(&view, message, now++));
                }
                char field[64] = "";
                if (source == 0) {
                    snprintf(field, sizeof(field), "\"emotion\":\"%s\",", tag);
                }
                snprintf(message, sizeof(message),
                    "{\"method\":\"caption\",\"params\":{%s%s"
                    "\"caption_type\":1,\"text\":\"A\",\"is_final\":false}}", id, field);
                (void)xiaotai_ai_view_apply(&view, message, now++);
                assert(view.emotion_tag_present && !strcmp(view.emotion, tag));
                assert(!view.emotion_tag_pending_caption);
                /* Finalize the same segment: its emotion must survive. */
                snprintf(message, sizeof(message),
                    "{\"method\":\"caption\",\"params\":{%s\"caption_type\":1,"
                    "\"text\":\"B\",\"mode\":%u,\"is_final\":true}}", id, mode);
                assert(xiaotai_ai_view_apply(&view, message, now++));
                assert(view.emotion_tag_present && !strcmp(view.emotion, tag));
                assert(!strcmp(view.caption, mode ? "AB" : "B"));
                assert(xiaotai_ai_view_apply(&view, "{\"method\":\"round_end\"}", now++));
                assert(view.emotion_tag_present); /* playback tail */
                /* New untagged partial after final: clear even within 100 ms.
                 * Unknown values cannot make a default into a server TAG. */
                snprintf(message, sizeof(message),
                    "{\"method\":\"caption\",\"params\":{%s\"caption_type\":1,"
                    "\"text\":\"C\",\"mode\":%u,\"emotion\":\"unsupported\","
                    "\"is_final\":false}}", id, mode);
                assert(xiaotai_ai_view_apply(&view, message, now++));
                assert(!view.emotion_tag_present && !strcmp(view.emotion, "happy"));
                assert(view.phase == XIAOTAI_AI_UI_SPEAKING);
                assert(!strcmp(view.caption, mode ? "ABC" : "C"));
                /* A new round must discard an unconsumed independent TAG. */
                snprintf(message, sizeof(message),
                    "{\"method\":\"emotion\",\"params\":{\"tag\":\"%s\"}}", tag);
                assert(xiaotai_ai_view_apply(&view, message, now++));
                assert(view.emotion_tag_pending_caption);
                assert(xiaotai_ai_view_apply(&view, "{\"method\":\"round_start\"}", now++));
                assert(!view.emotion_tag_present && !view.emotion_tag_pending_caption);
                /* Reinitializing a session cannot inherit the previous face. */
                assert(xiaotai_ai_view_apply(&view, message, now++));
                xiaotai_ai_view_init(&view);
                assert(!view.emotion_tag_present && !view.emotion_tag_pending_caption);
                assert(!view.caption_final && view.caption_type == -1);
                assert(view.caption[0] == '\0' && view.utterance_id[0] == '\0');
                ++cases;
            }
        }
    }
    printf("AI emotion segment combinations: %u\n", cases);
}

int main(void)
{
    test_emotion_segment_matrix();
    test_music_segments_do_not_inherit_completed_tag();
    test_default_face_is_not_a_server_tag();
    test_caption_utf8_capacity();
    test_history_protocol_sequences();
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
    assert(!view.emotion_tag_present);
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
    assert(view.emotion_tag_present);
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

    assert(xiaotai_ai_view_apply(&view,
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
    assert(strcmp(view.emotion, "happy") == 0);
    assert(!view.emotion_tag_present); /* new reply cannot inherit the last TAG */
    assert(xiaotai_ai_view_apply(&view,
        "{\"method\":\"round_start\",\"params\":{\"tag\":\"中性\"}}", 400U));
    assert(view.emotion_tag_present && !strcmp(view.emotion,"neutral"));
    assert(xiaotai_ai_view_apply(&view,
        "{\"method\":\"caption\",\"params\":{\"caption_type\":1,\"utterance_id\":\"u5\",\"text\":\"A\",\"is_final\":true}}", 401U));
    assert(view.emotion_tag_present); /* first caption preserves round_start TAG */
    assert(xiaotai_ai_view_apply(&view,"{\"method\":\"round_start\"}", 500U));
    assert(!view.emotion_tag_present && !strcmp(view.emotion,"happy"));
    assert(xiaotai_ai_view_apply(&view,
        "{\"method\":\"emotion\",\"params\":{\"tag\":\"unknown\"}}", 501U));
    assert(!view.emotion_tag_present);
    xiaotai_ai_view_init(&view);assert(!view.emotion_tag_present);
    assert(xiaotai_ai_view_apply(&view,
        "{\"method\":\"caption\",\"params\":{\"caption_type\":1,\"utterance_id\":\"a\",\"text\":\"A\",\"is_final\":true}}", 600U));
    assert(xiaotai_ai_view_apply(&view,
        "{\"method\":\"emotion\",\"params\":{\"tag\":\"angry\"}}", 601U));
    assert(xiaotai_ai_view_apply(&view,
        "{\"method\":\"caption\",\"params\":{\"caption_type\":1,\"utterance_id\":\"b\",\"text\":\"B\",\"is_final\":true}}", 602U));
    assert(view.emotion_tag_present && !strcmp(view.emotion,"angry"));
    assert(xiaotai_ai_view_apply(&view,
        "{\"method\":\"caption\",\"params\":{\"caption_type\":1,\"utterance_id\":\"c\",\"text\":\"C\",\"is_final\":true}}", 603U));
    assert(!view.emotion_tag_present); /* consume independent TAG once, not forever */

    assert(xiaotai_ai_view_apply(&view,
        "{\"method\":\"emotion\",\"params\":{\"tag\":\"sad\"}}", 604U));
    assert(xiaotai_ai_view_apply(&view,
        "{\"type\":\"ai_reply\",\"text\":\"legacy reply\"}", 605U));
    assert(view.emotion_tag_present && !strcmp(view.emotion,"sad"));
    assert(xiaotai_ai_view_apply(&view,
        "{\"type\":\"ai_reply\",\"text\":\"next reply\"}", 606U));
    assert(!view.emotion_tag_present);

    return 0;
}
