#include "xiaotai_audio_ns.h"

#include <string.h>

#include <common/bk_err.h>
#include <modules/audio_ns.h>

#define NS_SAMPLE_RATE_HZ 16000U
#define NS_FRAME_SAMPLES 320U
#define NS_MIN_SUPPRESSION_DB (-100)
#define NS_MAX_SUPPRESSION_DB 0

bool xiaotai_audio_ns_start(xiaotai_audio_ns_t *ns,
                            uint32_t sample_rate_hz,
                            size_t frame_samples,
                            int suppression_db)
{
    if (ns == NULL || sample_rate_hz != NS_SAMPLE_RATE_HZ ||
        frame_samples != NS_FRAME_SAMPLES ||
        suppression_db < NS_MIN_SUPPRESSION_DB ||
        suppression_db > NS_MAX_SUPPRESSION_DB) return false;

    memset(ns, 0, sizeof(*ns));
    if (bk_aud_ns_init((int)frame_samples, (int)sample_rate_hz) != BK_OK) {
        return false;
    }
    ns->initialized = true;
    if (bk_aud_ns_set_suprs(suppression_db) != BK_OK) {
        xiaotai_audio_ns_stop(ns);
        return false;
    }
    ns->sample_rate_hz = sample_rate_hz;
    ns->frame_samples = frame_samples;
    ns->suppression_db = suppression_db;
    return true;
}

bool xiaotai_audio_ns_process(xiaotai_audio_ns_t *ns,
                              int16_t *samples,
                              size_t sample_count,
                              bool *speech)
{
    if (speech != NULL) *speech = false;
    if (!xiaotai_audio_ns_ready(ns) || samples == NULL ||
        sample_count != ns->frame_samples) return false;

    int result = bk_aud_ns_process(samples);
    if (result != 0 && result != 1) return false;
    if (speech != NULL) *speech = result == 1;
    return true;
}

bool xiaotai_audio_ns_ready(const xiaotai_audio_ns_t *ns)
{
    return ns != NULL && ns->initialized &&
           ns->sample_rate_hz == NS_SAMPLE_RATE_HZ &&
           ns->frame_samples == NS_FRAME_SAMPLES;
}

void xiaotai_audio_ns_stop(xiaotai_audio_ns_t *ns)
{
    if (ns == NULL) return;
    if (ns->initialized) (void)bk_aud_ns_deinit();
    memset(ns, 0, sizeof(*ns));
}
