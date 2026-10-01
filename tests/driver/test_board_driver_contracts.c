#include <assert.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "xiaotai_board_audio.h"
#include "xiaotai_board_camera.h"

typedef struct {
    bool started;
    bool stop_requested;
    unsigned starts;
    unsigned stops;
    uint32_t rate;
} fake_audio_t;

static int fake_configure(void *context, uint32_t rate, size_t frame_samples)
{
    fake_audio_t *fake = context;
    if (fake->started || (rate != 8000U && rate != 16000U) ||
        frame_samples != 320U) return -1;
    fake->rate = rate;
    return 0;
}

static int fake_start(void *context)
{
    fake_audio_t *fake = context;
    if (fake->started || fake->rate == 0U) return -1;
    fake->started = true;
    fake->stop_requested = false;
    ++fake->starts;
    return 0;
}

static int fake_request_stop(void *context)
{
    fake_audio_t *fake = context;
    if (!fake->started) return -1;
    fake->stop_requested = true;
    return 0;
}

static int fake_stop(void *context)
{
    fake_audio_t *fake = context;
    if (!fake->started || !fake->stop_requested) return -1;
    fake->started = false;
    ++fake->stops;
    return 0;
}

static bool fake_ready(void *context)
{
    return ((fake_audio_t *)context)->started;
}

static xiaotai_board_audio_format_t fake_format(void *context)
{
    const fake_audio_t *fake = context;
    return (xiaotai_board_audio_format_t) {
        .sample_rate_hz = fake->rate,
        .capture_channels = 1U,
        .playback_channels = 1U,
        .capture_layout = XIAOTAI_CAPTURE_MONO_MIC,
    };
}

static int fake_read(void *context, int16_t *samples, size_t frames,
                     size_t *bytes_read)
{
    fake_audio_t *fake = context;
    if (!fake->started || fake->stop_requested) {
        if (bytes_read != NULL) *bytes_read = 0U;
        return -1;
    }
    memset(samples, 0, frames * sizeof(*samples));
    if (bytes_read != NULL) *bytes_read = frames * sizeof(*samples);
    return 0;
}

static int fake_write(void *context, const int16_t *samples, size_t frames,
                      size_t *bytes_written)
{
    fake_audio_t *fake = context;
    if (!fake->started || fake->stop_requested || samples == NULL) {
        if (bytes_written != NULL) *bytes_written = 0U;
        return -1;
    }
    if (bytes_written != NULL) *bytes_written = frames * sizeof(*samples);
    return 0;
}

static void exercise_audio_lifecycle(void)
{
    fake_audio_t fake = {0};
    xiaotai_board_audio_adapter_t adapter = {
        .context = &fake,
        .configure = fake_configure,
        .start = fake_start,
        .request_stop = fake_request_stop,
        .stop = fake_stop,
        .ready = fake_ready,
        .format = fake_format,
        .read_pcm = fake_read,
        .write_pcm = fake_write,
    };
    assert(xiaotai_board_audio_adapter_is_valid(&adapter));

    int16_t pcm[320] = {0};
    for (unsigned cycle = 0U; cycle < 100U; ++cycle) {
        uint32_t rate = (cycle & 1U) == 0U ? 8000U : 16000U;
        assert(adapter.configure(adapter.context, rate, 320U) == 0);
        assert(adapter.start(adapter.context) == 0);
        assert(adapter.start(adapter.context) < 0);
        assert(adapter.ready(adapter.context));
        xiaotai_board_audio_format_t format = adapter.format(adapter.context);
        assert(xiaotai_board_audio_format_is_valid(&format));
        assert(format.sample_rate_hz == rate);
        size_t transferred = 0U;
        assert(adapter.read_pcm(adapter.context, pcm, 320U, &transferred) == 0);
        assert(transferred == sizeof(pcm));
        assert(adapter.write_pcm(adapter.context, pcm, 320U, &transferred) == 0);
        assert(transferred == sizeof(pcm));
        assert(adapter.request_stop(adapter.context) == 0);
        assert(adapter.read_pcm(adapter.context, pcm, 320U, &transferred) < 0);
        assert(transferred == 0U);
        assert(adapter.stop(adapter.context) == 0);
        assert(adapter.stop(adapter.context) < 0);
        assert(!adapter.ready(adapter.context));
    }
    assert(fake.starts == 100U && fake.stops == 100U);
}

static void reject_partial_adapters(void)
{
    xiaotai_board_audio_adapter_t audio = {0};
    assert(!xiaotai_board_audio_adapter_is_valid(NULL));
    assert(!xiaotai_board_audio_adapter_is_valid(&audio));
    audio.start = fake_start;
    audio.ready = fake_ready;
    audio.format = fake_format;
    audio.read_pcm = fake_read;
    audio.write_pcm = fake_write;
    assert(xiaotai_board_audio_adapter_is_valid(&audio));
    audio.request_stop = fake_request_stop;
    assert(!xiaotai_board_audio_adapter_is_valid(&audio));

    xiaotai_board_camera_adapter_t camera = {0};
    assert(!xiaotai_board_camera_adapter_is_valid(NULL));
    assert(!xiaotai_board_camera_adapter_is_valid(&camera));
}

static void validate_camera_frame_contract(void)
{
    uint8_t data[4] = {1U, 2U, 3U, 4U};
    int token = 1;
    xiaotai_board_camera_frame_t frame = {
        .data = data,
        .size = sizeof(data),
        .width = 2U,
        .height = 2U,
        .format = XIAOTAI_CAMERA_FRAME_RGB565,
        .token = &token,
    };
    assert(xiaotai_board_camera_frame_is_valid(&frame));
    frame.token = NULL;
    assert(!xiaotai_board_camera_frame_is_valid(&frame));
}

static void validate_audio_format_contract(void)
{
    xiaotai_board_audio_format_t format = {
        .sample_rate_hz = 16000U,
        .capture_channels = 1U,
        .playback_channels = 1U,
        .capture_layout = XIAOTAI_CAPTURE_MONO_MIC,
    };
    assert(xiaotai_board_audio_format_is_valid(&format));
    assert(xiaotai_board_audio_capture_bytes_for_frames(&format, 320U) ==
           640U);
    assert(xiaotai_board_audio_playback_bytes_for_frames(&format, 320U) ==
           640U);

    format.capture_channels = 2U;
    assert(!xiaotai_board_audio_format_is_valid(&format));
    format.capture_layout = XIAOTAI_CAPTURE_MIC_PLAYBACK_REFERENCE;
    assert(xiaotai_board_audio_format_is_valid(&format));
    assert(xiaotai_board_audio_capture_bytes_for_frames(&format, 160U) ==
           640U);
    format.capture_channels = 1U;
    assert(!xiaotai_board_audio_format_is_valid(&format));
    format.capture_layout = (xiaotai_capture_layout_t)99;
    assert(!xiaotai_board_audio_format_is_valid(&format));
    assert(xiaotai_board_audio_capture_bytes_for_frames(NULL, 320U) == 0U);
    assert(xiaotai_board_audio_playback_bytes_for_frames(NULL, 320U) == 0U);
}

int main(void)
{
    exercise_audio_lifecycle();
    reject_partial_adapters();
    validate_audio_format_contract();
    validate_camera_frame_contract();
    return 0;
}
