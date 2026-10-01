#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* Capture channels describe semantic positions, not merely an I/O width.
 * Adapters must select the layout that matches the samples they return. */
typedef enum {
    XIAOTAI_CAPTURE_MONO_MIC = 1,
    XIAOTAI_CAPTURE_DUPLICATED_MIC = 2,
    XIAOTAI_CAPTURE_MIC_PLAYBACK_REFERENCE = 3,
} xiaotai_capture_layout_t;

typedef struct {
    uint32_t sample_rate_hz;
    uint8_t capture_channels;
    uint8_t playback_channels;
    xiaotai_capture_layout_t capture_layout;
} xiaotai_board_audio_format_t;

typedef struct {
    void *context;
    /* Optional session format selection. Call only while the adapter is
     * stopped. Adapters without this hook expose one fixed format. */
    int (*configure)(void *context, uint32_t sample_rate_hz,
                     size_t frame_samples);
    int (*start)(void *context);
    /* request_stop must make blocked read/write calls return, but must not
     * release storage that an in-flight caller may still reference.  stop is
     * called after the platform workers have quiesced and owns final cleanup.
     * Always-on adapters may leave both operations NULL. */
    int (*request_stop)(void *context);
    int (*stop)(void *context);
    bool (*ready)(void *context);
    xiaotai_board_audio_format_t (*format)(void *context);
    int (*read_pcm)(void *context, int16_t *samples, size_t frames,
                    size_t *bytes_read);
    int (*write_pcm)(void *context, const int16_t *samples, size_t frames,
                     size_t *bytes_written);
    /* Optional capture front-end control. Values are board-native and must be
     * validated by the adapter before touching the active ADC. */
    int (*set_capture_gain)(void *context, unsigned gain);
    int (*set_volume)(void *context, unsigned percent);
    int (*set_muted)(void *context, bool muted);
} xiaotai_board_audio_adapter_t;

/* Validate the callable surface before a media worker starts. request_stop
 * and stop form one ownership pair: adapters are either always-on (both NULL)
 * or provide both phases so blocked I/O can quiesce before final cleanup. */
static inline bool xiaotai_board_audio_adapter_is_valid(
    const xiaotai_board_audio_adapter_t *adapter)
{
    if (adapter == NULL || adapter->start == NULL || adapter->ready == NULL ||
        adapter->format == NULL || adapter->read_pcm == NULL ||
        adapter->write_pcm == NULL) {
        return false;
    }
    return (adapter->request_stop == NULL) == (adapter->stop == NULL);
}

/* Centralize layout invariants so platform media modules cannot silently
 * reinterpret a playback-reference channel as a second microphone. */
static inline bool xiaotai_board_audio_format_is_valid(
    const xiaotai_board_audio_format_t *format)
{
    if (format == NULL || format->sample_rate_hz == 0U ||
        format->capture_channels == 0U || format->playback_channels == 0U) {
        return false;
    }
    switch (format->capture_layout) {
    case XIAOTAI_CAPTURE_MONO_MIC:
        return format->capture_channels == 1U;
    case XIAOTAI_CAPTURE_DUPLICATED_MIC:
    case XIAOTAI_CAPTURE_MIC_PLAYBACK_REFERENCE:
        return format->capture_channels == 2U;
    default:
        return false;
    }
}

static inline size_t xiaotai_board_audio_capture_bytes_for_frames(
    const xiaotai_board_audio_format_t *format, size_t frames)
{
    return format == NULL ? 0U :
        frames * (size_t)format->capture_channels * sizeof(int16_t);
}

static inline size_t xiaotai_board_audio_playback_bytes_for_frames(
    const xiaotai_board_audio_format_t *format, size_t frames)
{
    return format == NULL ? 0U :
        frames * (size_t)format->playback_channels * sizeof(int16_t);
}
