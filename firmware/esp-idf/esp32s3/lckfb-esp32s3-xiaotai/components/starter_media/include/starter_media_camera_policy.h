#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "sensor.h"

/* Keep this policy evidence-backed: add only sensors observed on this board. */
static inline bool starter_media_camera_sensor_supported(uint16_t pid)
{
    return pid == GC0308_PID || pid == GC2145_PID;
}

static inline const char *starter_media_camera_sensor_name(uint16_t pid)
{
    switch (pid) {
    case GC0308_PID:
        return "GC0308";
    case GC2145_PID:
        return "GC2145";
    default:
        return "unsupported";
    }
}
