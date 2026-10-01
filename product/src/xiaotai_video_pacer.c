#include "xiaotai_video_pacer.h"

#include <stddef.h>

void xiaotai_video_pacer_init(xiaotai_video_pacer_t *pacer,
                               uint32_t source_fps,
                               uint32_t target_fps)
{
    if (pacer == NULL) return;
    pacer->source_fps = source_fps;
    pacer->target_fps = target_fps;
    pacer->credit = 0U;
}

void xiaotai_video_pacer_set_target(xiaotai_video_pacer_t *pacer,
                                    uint32_t target_fps)
{
    if (pacer == NULL || pacer->target_fps == target_fps) return;
    pacer->target_fps = target_fps;
    pacer->credit = 0U;
}

bool xiaotai_video_pacer_should_send(xiaotai_video_pacer_t *pacer,
                                     bool key_frame)
{
    if (pacer == NULL || pacer->source_fps == 0U ||
        pacer->target_fps == 0U ||
        pacer->target_fps >= pacer->source_fps) {
        return true;
    }
    if (key_frame) {
        /* Pacing never intentionally removes decoder recovery frames. Resetting
         * the credit
         * prevents the frame after a forced keyframe from creating a burst. */
        pacer->credit = 0U;
        return true;
    }
    pacer->credit += pacer->target_fps;
    if (pacer->credit < pacer->source_fps) return false;
    pacer->credit -= pacer->source_fps;
    return true;
}
