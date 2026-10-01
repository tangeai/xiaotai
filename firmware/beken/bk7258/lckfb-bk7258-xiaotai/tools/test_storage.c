#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include "easyflash.h"
#include "xiaotai_audio_policy.h"
#include "xiaotai_storage.h"

#define RECORD_MAGIC 0x5849414FU

static uint8_t blob[512];
static size_t blob_size;

size_t ef_get_env_blob(const char *key, void *value, size_t capacity,
                       size_t *saved_size)
{
    (void)key;
    *saved_size = blob_size;
    size_t copied = blob_size < capacity ? blob_size : capacity;
    memcpy(value, blob, copied);
    return copied;
}

EfErrCode ef_set_env_blob(const char *key, const void *value, size_t size)
{
    (void)key;
    assert(size <= sizeof(blob));
    memcpy(blob, value, size);
    blob_size = size;
    return EF_NO_ERR;
}

EfErrCode ef_del_env(const char *key)
{
    (void)key;
    blob_size = 0;
    return EF_NO_ERR;
}

uint32_t ef_calc_crc32(uint32_t crc, const void *data, size_t size)
{
    const uint8_t *bytes = data;
    for (size_t i = 0; i < size; ++i) crc = crc * 33U + bytes[i];
    return crc;
}

typedef struct {
    uint32_t magic;
    uint16_t version;
    uint16_t payload_size;
    uint32_t payload_crc;
} header_t;

typedef struct {
    uint8_t volume;
    bool speaker_muted;
    bool microphone_muted;
    uint8_t screen_timeout_index;
    uint8_t home_face;
} legacy_settings_t;

typedef struct {
    header_t header;
    legacy_settings_t payload;
} legacy_record_t;

int main(void)
{
    legacy_record_t legacy = {
        .header = {
            .magic = RECORD_MAGIC,
            .version = 1,
            .payload_size = sizeof(legacy_settings_t),
        },
        .payload = {
            .volume = 6,
            .speaker_muted = true,
            .screen_timeout_index = 3,
            .home_face = 2,
        },
    };
    legacy.header.payload_crc = ef_calc_crc32(
        0, &legacy.payload, sizeof(legacy.payload));
    memcpy(blob, &legacy, sizeof(legacy));
    blob_size = sizeof(legacy);

    xiaotai_product_settings_t settings = {0};
    assert(xiaotai_storage_load_settings(&settings) == 0);
    assert(settings.volume == 6 && settings.speaker_muted);
    assert(settings.screen_timeout_index == 3 && settings.home_face == 2);
    assert(settings.microphone_sensitivity ==
           XIAOTAI_MIC_SENSITIVITY_DEFAULT);

    settings.microphone_sensitivity = 5;
    assert(xiaotai_storage_save_settings(&settings) == 0);
    memset(&settings, 0, sizeof(settings));
    assert(xiaotai_storage_load_settings(&settings) == 0);
    assert(settings.microphone_sensitivity == 5);

    blob[0] ^= 1U;
    assert(xiaotai_storage_load_settings(&settings) != 0);
    assert(settings.microphone_sensitivity ==
           XIAOTAI_MIC_SENSITIVITY_DEFAULT);
    return 0;
}
