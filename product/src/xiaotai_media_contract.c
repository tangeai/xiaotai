#include "xiaotai_media_contract.h"

#include <stddef.h>

bool xiaotai_media_contract_get(xiaotai_media_mode_t mode,
                                xiaotai_media_contract_t *out)
{
    if (out == NULL) return false;

    switch (mode) {
    case XIAOTAI_MEDIA_MODE_STREAM:
        *out = (xiaotai_media_contract_t) {
            .up_audio_stream_id = 10U,
            .up_video_stream_id = 11U,
            .down_audio_stream_id = 14U,
            .down_video_stream_id = 15U,
            .sample_rate_hz = 8000U,
            .frame_samples = 320U,
            .codec = XIAOTAI_MEDIA_CODEC_ALAW,
            .has_video = true,
        };
        return true;
    case XIAOTAI_MEDIA_MODE_AI:
        *out = (xiaotai_media_contract_t) {
            .up_audio_stream_id = 1U,
            .up_video_stream_id = XIAOTAI_MEDIA_STREAM_UNUSED,
            .down_audio_stream_id = 1U,
            .down_video_stream_id = XIAOTAI_MEDIA_STREAM_UNUSED,
            .sample_rate_hz = 16000U,
            .frame_samples = 320U,
            .codec = XIAOTAI_MEDIA_CODEC_OPUS,
            .has_video = false,
        };
        return true;
    case XIAOTAI_MEDIA_MODE_DEVICE_CALL:
        *out = (xiaotai_media_contract_t) {
            .up_audio_stream_id = 10U,
            .up_video_stream_id = 11U,
            .down_audio_stream_id = 10U,
            .down_video_stream_id = 11U,
            .sample_rate_hz = 8000U,
            .frame_samples = 320U,
            .codec = XIAOTAI_MEDIA_CODEC_ALAW,
            .has_video = true,
        };
        return true;
    case XIAOTAI_MEDIA_MODE_WECHAT_VOIP:
        *out = (xiaotai_media_contract_t) {
            .up_audio_stream_id = 0U,
            .up_video_stream_id = 1U,
            .down_audio_stream_id = 0U,
            .down_video_stream_id = 1U,
            .sample_rate_hz = 8000U,
            .frame_samples = 320U,
            .codec = XIAOTAI_MEDIA_CODEC_ALAW,
            .has_video = true,
        };
        return true;
    case XIAOTAI_MEDIA_MODE_ROOM:
        *out = (xiaotai_media_contract_t) {
            .up_audio_stream_id = 1U,
            .up_video_stream_id = XIAOTAI_MEDIA_STREAM_UNUSED,
            .down_audio_stream_id = 1U,
            .down_video_stream_id = XIAOTAI_MEDIA_STREAM_UNUSED,
            .sample_rate_hz = 8000U,
            .frame_samples = 320U,
            .codec = XIAOTAI_MEDIA_CODEC_ALAW,
            .has_video = false,
        };
        return true;
    case XIAOTAI_MEDIA_MODE_INVALID:
    default:
        return false;
    }
}
