#ifndef XIAOTAI_AI_FACE_POLICY_H
#define XIAOTAI_AI_FACE_POLICY_H

#include <stdbool.h>
#include "xiaotai_ai_view.h"

typedef enum {
    XIAOTAI_AI_FACE_DEFAULT = 0,
    XIAOTAI_AI_FACE_TAG,
    XIAOTAI_AI_FACE_DOTS,
} xiaotai_ai_face_mode_t;

static inline xiaotai_ai_face_mode_t xiaotai_ai_face_mode(
    xiaotai_ai_ui_phase_t phase, bool emotion_tag_present)
{
    if (emotion_tag_present) return XIAOTAI_AI_FACE_TAG;
    return phase == XIAOTAI_AI_UI_SPEAKING ? XIAOTAI_AI_FACE_DOTS :
           XIAOTAI_AI_FACE_DEFAULT;
}

#endif
