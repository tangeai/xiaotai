#ifndef XIAOTAI_AI_FEEDBACK_H
#define XIAOTAI_AI_FEEDBACK_H

/* Shared product feedback. Platform renderers may display title and hint on
 * separate lines; leaving a page never changes the global privacy setting. */
#define XIAOTAI_AI_GLOBAL_MUTE_TITLE "麦克风已关闭"
#define XIAOTAI_AI_GLOBAL_MUTE_HINT "请在设置中开启"
#define XIAOTAI_AI_GLOBAL_MUTE_MESSAGE \
    XIAOTAI_AI_GLOBAL_MUTE_TITLE "，" XIAOTAI_AI_GLOBAL_MUTE_HINT
#define XIAOTAI_AI_GLOBAL_MUTE_REASON \
    "global microphone muted; enable microphone in Settings"

#endif
