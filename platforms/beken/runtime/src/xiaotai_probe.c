#include "tirtc_bk_probe.h"

#include <string.h>

#include "xiaotai_ui.h"

const void *tirtc_bk_probe_active_lcd(void)
{
    return xiaotai_ui_probe_lcd();
}

bool tirtc_bk_probe_audio(tirtc_bk_audio_snapshot_t *out)
{
    if (out == NULL) return false;
    memset(out, 0, sizeof(*out));
    out->microphone.present = true;
    out->microphone.channels = 1;
    out->microphone.sample_rate_hz = 8000;
    out->microphone.sample_bits = 16;
    out->speaker.present = true;
    out->speaker.channels = 1;
    out->speaker.sample_rate_hz = 8000;
    out->speaker.sample_bits = 16;
    out->simultaneous = true;
    /* The audio pipeline feeds the post-volume PCM submitted to the DAC into
     * the BK software AEC.  Availability is a build/runtime capability;
     * effectiveness remains NOT_RUN until the acoustic HIL test passes. */
    out->playback_reference = true;
    out->aec_available = true;
    out->aec_test = TIRTC_BK_TEST_NOT_RUN;
    return true;
}

uint32_t tirtc_bk_probe_application_usable_bytes(void)
{
    /* primary_ap_app logical payload from auto_partitions.csv. */
    return 2720U * 1024U;
}
