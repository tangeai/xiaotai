#ifndef XIAOTAI_PLATFORM_H
#define XIAOTAI_PLATFORM_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/*
 * Stable seam between XiaoTai product behavior and BK7258 implementation.
 * Implementations own Beken handles and buffers; the product layer sees only
 * bounded values and encoded media frames.
 */

typedef struct {
    char device_id[65];
    char device_secret[257];
    char client_id[65];
} xiaotai_device_credentials_t;

typedef struct {
    uint8_t stream_id;
    uint8_t media_type;
    uint8_t flags;
    uint32_t timestamp_ms;
    const void *data;
    size_t size;
} xiaotai_media_frame_t;

typedef void (*xiaotai_media_sink_fn)(const xiaotai_media_frame_t *frame,
                                      void *context);

typedef struct {
    int (*network_start)(void);
    bool (*network_ready)(void);
    int (*provisioning_start)(void);
    int (*credentials_load)(xiaotai_device_credentials_t *out);
    int (*credentials_save)(const xiaotai_device_credentials_t *value);
    int (*binding_run)(xiaotai_device_credentials_t *out);
    uint64_t (*monotonic_ms)(void);
    int (*audio_start)(xiaotai_media_sink_fn sink, void *context);
    void (*audio_stop)(void);
    int (*video_start)(xiaotai_media_sink_fn sink, void *context);
    void (*video_stop)(void);
} xiaotai_platform_ops_t;

#endif

