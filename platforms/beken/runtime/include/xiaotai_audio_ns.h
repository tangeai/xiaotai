#ifndef XIAOTAI_AUDIO_NS_H
#define XIAOTAI_AUDIO_NS_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef struct {
    uint32_t sample_rate_hz;
    size_t frame_samples;
    int suppression_db;
    bool initialized;
} xiaotai_audio_ns_t;

bool xiaotai_audio_ns_start(xiaotai_audio_ns_t *ns,
                            uint32_t sample_rate_hz,
                            size_t frame_samples,
                            int suppression_db);
bool xiaotai_audio_ns_process(xiaotai_audio_ns_t *ns,
                              int16_t *samples,
                              size_t sample_count,
                              bool *speech);
bool xiaotai_audio_ns_ready(const xiaotai_audio_ns_t *ns);
void xiaotai_audio_ns_stop(xiaotai_audio_ns_t *ns);

#endif
