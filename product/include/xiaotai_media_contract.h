#ifndef XIAOTAI_MEDIA_CONTRACT_H
#define XIAOTAI_MEDIA_CONTRACT_H

#include <stdbool.h>
#include <stdint.h>

#define XIAOTAI_MEDIA_STREAM_UNUSED 0xffU

typedef enum {
    XIAOTAI_MEDIA_MODE_INVALID = 0,
    XIAOTAI_MEDIA_MODE_STREAM,
    XIAOTAI_MEDIA_MODE_AI,
    XIAOTAI_MEDIA_MODE_DEVICE_CALL,
    XIAOTAI_MEDIA_MODE_WECHAT_VOIP,
    XIAOTAI_MEDIA_MODE_ROOM,
} xiaotai_media_mode_t;

typedef enum {
    XIAOTAI_MEDIA_CODEC_ALAW = 1,
    XIAOTAI_MEDIA_CODEC_OPUS,
} xiaotai_media_codec_t;

typedef struct {
    uint8_t up_audio_stream_id;
    uint8_t up_video_stream_id;
    uint8_t down_audio_stream_id;
    uint8_t down_video_stream_id;
    uint32_t sample_rate_hz;
    uint16_t frame_samples;
    xiaotai_media_codec_t codec;
    bool has_video;
} xiaotai_media_contract_t;

/* One product-level source of truth for negotiated media identities. Board
 * adapters may change physical I/O chunking, but not these wire contracts. */
bool xiaotai_media_contract_get(xiaotai_media_mode_t mode,
                                xiaotai_media_contract_t *out);

#endif
