#ifndef XIAOTAI_VIDEO_PACER_H
#define XIAOTAI_VIDEO_PACER_H

#include <stdbool.h>
#include <stdint.h>

typedef struct {
    uint32_t source_fps;
    uint32_t target_fps;
    uint32_t credit;
} xiaotai_video_pacer_t;

void xiaotai_video_pacer_init(xiaotai_video_pacer_t *pacer,
                               uint32_t source_fps,
                               uint32_t target_fps);
void xiaotai_video_pacer_set_target(xiaotai_video_pacer_t *pacer,
                                    uint32_t target_fps);
bool xiaotai_video_pacer_should_send(xiaotai_video_pacer_t *pacer,
                                     bool key_frame);

#endif
