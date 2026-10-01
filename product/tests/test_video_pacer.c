#include <assert.h>

#include "xiaotai_video_pacer.h"

int main(void)
{
    xiaotai_video_pacer_t pacer;
    xiaotai_video_pacer_init(&pacer, 20U, 15U);

    unsigned sent = 0U;
    for (unsigned i = 0U; i < 20U; ++i) {
        if (xiaotai_video_pacer_should_send(&pacer, false)) ++sent;
    }
    assert(sent == 15U);

    /* Decoder recovery frames are never intentionally dropped. */
    assert(xiaotai_video_pacer_should_send(&pacer, true));
    xiaotai_video_pacer_set_target(&pacer, 20U);
    for (unsigned i = 0U; i < 20U; ++i) {
        assert(xiaotai_video_pacer_should_send(&pacer, false));
    }
    xiaotai_video_pacer_set_target(&pacer, 0U);
    assert(xiaotai_video_pacer_should_send(&pacer, false));
    return 0;
}
