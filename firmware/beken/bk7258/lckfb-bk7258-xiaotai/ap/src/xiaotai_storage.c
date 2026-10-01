#include "xiaotai_storage.h"

#include <common/bk_err.h>
#include <easyflash.h>
#include <stdint.h>
#include <string.h>

#include "xiaotai_audio_policy.h"

#define XIAOTAI_RECORD_MAGIC 0x5849414FU
#define XIAOTAI_RECORD_VERSION 1U
#define XIAOTAI_WIFI_KEY "xiaotai_wifi_v1"
#define XIAOTAI_DEVICE_KEY "xiaotai_device_v1"
#define XIAOTAI_SETTINGS_KEY "xiaotai_settings_v1"

typedef struct {
    uint32_t magic;
    uint16_t version;
    uint16_t payload_size;
    uint32_t payload_crc;
} xiaotai_record_header_t;

typedef struct {
    xiaotai_record_header_t header;
    xiaotai_wifi_credentials_t payload;
} xiaotai_wifi_record_t;

typedef struct {
    xiaotai_record_header_t header;
    xiaotai_device_credentials_t payload;
} xiaotai_device_record_t;

typedef struct {
    xiaotai_record_header_t header;
    xiaotai_product_settings_t payload;
} xiaotai_settings_record_t;

typedef struct {
    uint8_t volume;
    bool speaker_muted;
    bool microphone_muted;
    uint8_t screen_timeout_index;
    uint8_t home_face;
} xiaotai_product_settings_v1_t;

static bool terminated(const char *text, size_t capacity)
{
    return text != NULL && memchr(text, '\0', capacity) != NULL;
}

bool xiaotai_wifi_credentials_valid(const xiaotai_wifi_credentials_t *value)
{
    if (value == NULL ||
        !terminated(value->ssid, sizeof(value->ssid)) ||
        !terminated(value->password, sizeof(value->password))) {
        return false;
    }
    size_t ssid_length = strlen(value->ssid);
    size_t password_length = strlen(value->password);
    return ssid_length >= 1U && ssid_length <= XIAOTAI_WIFI_SSID_MAX &&
           (password_length == 0U ||
            (password_length >= 8U &&
             password_length <= XIAOTAI_WIFI_PASSWORD_MAX));
}

static bool device_credentials_valid(const xiaotai_device_credentials_t *value)
{
    return value != NULL &&
           terminated(value->device_id, sizeof(value->device_id)) &&
           terminated(value->device_secret, sizeof(value->device_secret)) &&
           terminated(value->client_id, sizeof(value->client_id)) &&
           value->device_id[0] != '\0' && value->device_secret[0] != '\0';
}

static void make_header(xiaotai_record_header_t *header,
                        const void *payload,
                        size_t payload_size)
{
    header->magic = XIAOTAI_RECORD_MAGIC;
    header->version = XIAOTAI_RECORD_VERSION;
    header->payload_size = (uint16_t)payload_size;
    header->payload_crc = ef_calc_crc32(0, payload, payload_size);
}

static bool header_valid(const xiaotai_record_header_t *header,
                         const void *payload,
                         size_t payload_size)
{
    return header->magic == XIAOTAI_RECORD_MAGIC &&
           header->version == XIAOTAI_RECORD_VERSION &&
           header->payload_size == payload_size &&
           header->payload_crc == ef_calc_crc32(0, payload, payload_size);
}

int xiaotai_storage_load_wifi(xiaotai_wifi_credentials_t *out)
{
    if (out == NULL) return BK_ERR_PARAM;
    xiaotai_wifi_record_t record = {0};
    size_t saved = 0;
    size_t read = ef_get_env_blob(XIAOTAI_WIFI_KEY,
                                  &record,
                                  sizeof(record),
                                  &saved);
    if (read != sizeof(record) || saved != sizeof(record) ||
        !header_valid(&record.header, &record.payload, sizeof(record.payload)) ||
        !xiaotai_wifi_credentials_valid(&record.payload)) {
        memset(out, 0, sizeof(*out));
        return BK_FAIL;
    }
    *out = record.payload;
    return BK_OK;
}

int xiaotai_storage_save_wifi(const xiaotai_wifi_credentials_t *value)
{
    if (!xiaotai_wifi_credentials_valid(value)) return BK_ERR_PARAM;
    xiaotai_wifi_record_t record = {0};
    record.payload = *value;
    make_header(&record.header, &record.payload, sizeof(record.payload));
    return ef_set_env_blob(XIAOTAI_WIFI_KEY, &record, sizeof(record)) == EF_NO_ERR
               ? BK_OK : BK_FAIL;
}

int xiaotai_storage_clear_wifi(void)
{
    EfErrCode rc = ef_del_env(XIAOTAI_WIFI_KEY);
    return rc == EF_NO_ERR || rc == EF_ENV_NAME_ERR ? BK_OK : BK_FAIL;
}

int xiaotai_storage_load_device(xiaotai_device_credentials_t *out)
{
    if (out == NULL) return BK_ERR_PARAM;
    xiaotai_device_record_t record = {0};
    size_t saved = 0;
    size_t read = ef_get_env_blob(XIAOTAI_DEVICE_KEY,
                                  &record,
                                  sizeof(record),
                                  &saved);
    if (read != sizeof(record) || saved != sizeof(record) ||
        !header_valid(&record.header, &record.payload, sizeof(record.payload)) ||
        !device_credentials_valid(&record.payload)) {
        memset(out, 0, sizeof(*out));
        return BK_FAIL;
    }
    *out = record.payload;
    return BK_OK;
}

int xiaotai_storage_save_device(const xiaotai_device_credentials_t *value)
{
    if (!device_credentials_valid(value)) return BK_ERR_PARAM;
    xiaotai_device_record_t record = {0};
    record.payload = *value;
    make_header(&record.header, &record.payload, sizeof(record.payload));
    return ef_set_env_blob(XIAOTAI_DEVICE_KEY, &record, sizeof(record)) == EF_NO_ERR
               ? BK_OK : BK_FAIL;
}

int xiaotai_storage_clear_device(void)
{
    EfErrCode rc = ef_del_env(XIAOTAI_DEVICE_KEY);
    return rc == EF_NO_ERR || rc == EF_ENV_NAME_ERR ? BK_OK : BK_FAIL;
}

void xiaotai_product_settings_default(xiaotai_product_settings_t *out)
{
    if (out == NULL) return;
    *out = (xiaotai_product_settings_t){
        .volume = 7,
        .screen_timeout_index = 1, /* 5 minutes */
        .microphone_sensitivity = XIAOTAI_MIC_SENSITIVITY_DEFAULT,
    };
}

static bool settings_valid(const xiaotai_product_settings_t *value)
{
    return value != NULL && value->volume <= 10U &&
           value->screen_timeout_index <= 4U && value->home_face <= 2U &&
           value->microphone_sensitivity >= XIAOTAI_MIC_SENSITIVITY_MIN &&
           value->microphone_sensitivity <= XIAOTAI_MIC_SENSITIVITY_MAX;
}

int xiaotai_storage_load_settings(xiaotai_product_settings_t *out)
{
    if (out == NULL) return BK_ERR_PARAM;
    xiaotai_settings_record_t record = {0};
    size_t saved = 0;
    size_t read = ef_get_env_blob(XIAOTAI_SETTINGS_KEY, &record,
                                  sizeof(record), &saved);
    if (read == sizeof(record) && saved == sizeof(record) &&
        record.header.magic == XIAOTAI_RECORD_MAGIC &&
        record.header.version == XIAOTAI_RECORD_VERSION &&
        record.header.payload_size == sizeof(xiaotai_product_settings_v1_t) &&
        record.header.payload_crc == ef_calc_crc32(
            0, &record.payload, sizeof(xiaotai_product_settings_v1_t))) {
        xiaotai_product_settings_v1_t legacy = {0};
        memcpy(&legacy, &record.payload, sizeof(legacy));
        xiaotai_product_settings_default(out);
        out->volume = legacy.volume;
        out->speaker_muted = legacy.speaker_muted;
        out->microphone_muted = legacy.microphone_muted;
        out->screen_timeout_index = legacy.screen_timeout_index;
        out->home_face = legacy.home_face;
        if (settings_valid(out)) return BK_OK;
        xiaotai_product_settings_default(out);
        return BK_FAIL;
    }
    if (read != sizeof(record) || saved != sizeof(record) ||
        !header_valid(&record.header, &record.payload, sizeof(record.payload)) ||
        !settings_valid(&record.payload)) {
        xiaotai_product_settings_default(out);
        return BK_FAIL;
    }
    *out = record.payload;
    return BK_OK;
}

int xiaotai_storage_save_settings(const xiaotai_product_settings_t *value)
{
    if (!settings_valid(value)) return BK_ERR_PARAM;
    xiaotai_settings_record_t record = {0};
    record.payload = *value;
    make_header(&record.header, &record.payload, sizeof(record.payload));
    return ef_set_env_blob(XIAOTAI_SETTINGS_KEY, &record, sizeof(record)) ==
                   EF_NO_ERR ? BK_OK : BK_FAIL;
}
