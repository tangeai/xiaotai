#ifndef XIAOTAI_VIDEO_H
#define XIAOTAI_VIDEO_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef int (*xiaotai_video_uplink_fn)(const uint8_t *annexb,
                                       size_t size,
                                       uint32_t timestamp_ms,
                                       bool key_frame,
                                       void *context);

/** Reserve the DVP/H.264 controller while contiguous SRAM is still available. */
int xiaotai_video_init(void);
int xiaotai_video_start(xiaotai_video_uplink_fn uplink, void *context);
int xiaotai_video_request_keyframe(void);
int xiaotai_video_stop(void);
bool xiaotai_video_running(void);

#endif
