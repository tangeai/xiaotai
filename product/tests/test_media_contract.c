#include <assert.h>
#include <stddef.h>

#include "xiaotai_media_contract.h"

int main(void)
{
    xiaotai_media_contract_t contract;

    assert(xiaotai_media_contract_get(XIAOTAI_MEDIA_MODE_STREAM, &contract));
    assert(contract.up_audio_stream_id == 10U);
    assert(contract.up_video_stream_id == 11U);
    assert(contract.down_audio_stream_id == 14U);
    assert(contract.down_video_stream_id == 15U);
    assert(contract.sample_rate_hz == 8000U);
    assert(contract.frame_samples == 320U);
    assert(contract.codec == XIAOTAI_MEDIA_CODEC_ALAW);
    assert(contract.has_video);

    assert(xiaotai_media_contract_get(XIAOTAI_MEDIA_MODE_AI, &contract));
    assert(contract.up_audio_stream_id == 1U);
    assert(contract.down_audio_stream_id == 1U);
    assert(contract.sample_rate_hz == 16000U);
    assert(contract.frame_samples == 320U);
    assert(contract.codec == XIAOTAI_MEDIA_CODEC_OPUS);
    assert(!contract.has_video);

    assert(xiaotai_media_contract_get(XIAOTAI_MEDIA_MODE_DEVICE_CALL,
                                      &contract));
    assert(contract.up_audio_stream_id == 10U);
    assert(contract.down_audio_stream_id == 10U);
    assert(contract.up_video_stream_id == 11U);
    assert(contract.down_video_stream_id == 11U);
    assert(contract.sample_rate_hz == 8000U);
    assert(contract.frame_samples == 320U);
    assert(contract.codec == XIAOTAI_MEDIA_CODEC_ALAW);
    assert(contract.has_video);

    assert(xiaotai_media_contract_get(XIAOTAI_MEDIA_MODE_WECHAT_VOIP,
                                      &contract));
    assert(contract.up_audio_stream_id == 0U);
    assert(contract.down_audio_stream_id == 0U);
    assert(contract.up_video_stream_id == 1U);
    assert(contract.down_video_stream_id == 1U);
    assert(contract.sample_rate_hz == 8000U);
    assert(contract.codec == XIAOTAI_MEDIA_CODEC_ALAW);
    assert(contract.has_video);

    assert(xiaotai_media_contract_get(XIAOTAI_MEDIA_MODE_ROOM, &contract));
    assert(contract.up_audio_stream_id == 1U);
    assert(contract.down_audio_stream_id == 1U);
    assert(contract.up_video_stream_id == XIAOTAI_MEDIA_STREAM_UNUSED);
    assert(contract.down_video_stream_id == XIAOTAI_MEDIA_STREAM_UNUSED);
    assert(contract.sample_rate_hz == 8000U);
    assert(contract.frame_samples == 320U);
    assert(contract.codec == XIAOTAI_MEDIA_CODEC_ALAW);
    assert(!contract.has_video);

    assert(!xiaotai_media_contract_get(XIAOTAI_MEDIA_MODE_INVALID, &contract));
    assert(!xiaotai_media_contract_get((xiaotai_media_mode_t)99, &contract));
    assert(!xiaotai_media_contract_get(XIAOTAI_MEDIA_MODE_STREAM, NULL));
    return 0;
}
