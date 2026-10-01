#ifndef XIAOTAI_AI_VIEW_H
#define XIAOTAI_AI_VIEW_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>


typedef enum {
    XIAOTAI_AI_UI_LISTENING = 0,
    XIAOTAI_AI_UI_THINKING,
    XIAOTAI_AI_UI_SPEAKING,
} xiaotai_ai_ui_phase_t;

typedef struct {
    xiaotai_ai_ui_phase_t phase;
    int caption_type;
    char utterance_id[65];
    char emotion[16];
    char caption[256];
    uint32_t last_present_ms;
} xiaotai_ai_view_t;

/* Pure presentation state. It owns caption grouping, UTF-8 truncation,
 * stable emotion mapping and the 10 fps partial-caption throttle. */
void xiaotai_ai_view_init(xiaotai_ai_view_t *view);
/** Canonical AI emotion catalog shared by protocol state and UI adapters. */
size_t xiaotai_ai_emotion_count(void);
const char *xiaotai_ai_emotion_at(size_t index);
const char *xiaotai_ai_emotion_label_zh(const char *emotion);
void xiaotai_ai_view_set_emotion(xiaotai_ai_view_t *view,
                                 const char *emotion);
bool xiaotai_ai_view_apply(xiaotai_ai_view_t *view, const char *json,
                           uint32_t now_ms);

#endif
