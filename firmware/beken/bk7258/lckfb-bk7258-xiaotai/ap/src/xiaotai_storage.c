#include "xiaotai_storage.h"

#include <common/bk_err.h>
#include <easyflash.h>
#include <stdint.h>
#include <stdatomic.h>
#include <string.h>

#include "xiaotai_audio_policy.h"

#define XIAOTAI_RECORD_MAGIC 0x5849414FU
#define XIAOTAI_RECORD_VERSION 1U
#define XIAOTAI_WIFI_KEY "xiaotai_wifi_v1"
#define XIAOTAI_DEVICE_KEY "xiaotai_device_v1"
#define XIAOTAI_SETTINGS_KEY "xiaotai_settings_v1"
#define XIAOTAI_ENDPOINT_KEY "xiaotai_ep_v1"

static bool endpoint_valid(const char *url)
{
    if (url == NULL) return false;
    const char *host = NULL;
    if (strncmp(url, "http://", 7) == 0) host = url + 7;
    else if (strncmp(url, "https://", 8) == 0) host = url + 8;
    else return false;
    size_t length = 0;
    while (length < XIAOTAI_ENDPOINT_MAX && url[length] != '\0') ++length;
    if (length >= XIAOTAI_ENDPOINT_MAX - sizeof("/services") || *host == '\0') return false;
    const char *p = host;
    while ((*p >= 'a' && *p <= 'z') || (*p >= 'A' && *p <= 'Z') ||
           (*p >= '0' && *p <= '9') || *p == '.' || *p == '-') ++p;
    if (p == host) return false;
    if (*p == ':') {
        unsigned port = 0;
        if (*++p < '0' || *p > '9') return false;
        while (*p >= '0' && *p <= '9') {
            port = port * 10U + (unsigned)(*p++ - '0');
            if (port > 65535U) return false;
        }
        if (port == 0U) return false;
    }
    if (*p == '/') {
        for (; *p != '\0'; ++p) {
            unsigned char c = (unsigned char)*p;
            if (c <= 0x20 || c >= 0x7f || c == '?' || c == '#' || c == '\\') return false;
        }
    }
    return *p == '\0';
}

bool xiaotai_storage_format_discovery_url(const char *base, char *output, size_t capacity)
{
    if (base == NULL || output == NULL || capacity == 0) return false;
    size_t length = strlen(base);
    while (length > 0 && base[length - 1] == '/') --length;
    static const char suffix[] = "/services";
    if (length == 0 || length + sizeof(suffix) > capacity) return false;
    memcpy(output, base, length);
    memcpy(output + length, suffix, sizeof(suffix));
    return true;
}

/* EasyFlash already serializes ordinary writes. Only an explicit reset gates
 * new writers; an in-flight save makes reset fail for a safe user retry. */
static atomic_bool s_storage_reset_pending;
static atomic_uint s_storage_writers;

static bool storage_write_begin(void)
{
    if (atomic_load(&s_storage_reset_pending)) return false;
    atomic_fetch_add(&s_storage_writers, 1U);
    if (atomic_load(&s_storage_reset_pending)) {
        atomic_fetch_sub(&s_storage_writers, 1U);
        return false;
    }
    return true;
}

static int storage_write_blob(const char *key, const void *value, size_t size)
{
    if (!storage_write_begin()) return BK_FAIL;
    EfErrCode rc = ef_set_env_blob(key, value, size);
    atomic_fetch_sub(&s_storage_writers, 1U);
    return rc == EF_NO_ERR ? BK_OK : BK_FAIL;
}

static int storage_delete(const char *key)
{
    if (!storage_write_begin()) return BK_FAIL;
    EfErrCode rc = ef_del_env(key);
    atomic_fetch_sub(&s_storage_writers, 1U);
    return rc == EF_NO_ERR || rc == EF_ENV_NAME_ERR ? BK_OK : BK_FAIL;
}

int xiaotai_storage_reset_user_data(void)
{
    if (atomic_exchange(&s_storage_reset_pending, true)) return BK_FAIL;
    if (atomic_load(&s_storage_writers) != 0U) {
        atomic_store(&s_storage_reset_pending, false);
        return BK_FAIL;
    }
    const char *keys[] = {XIAOTAI_DEVICE_KEY, XIAOTAI_WIFI_KEY, XIAOTAI_SETTINGS_KEY,
                          XIAOTAI_ENDPOINT_KEY};
    int result = BK_OK;
    for (size_t i = 0; i < sizeof(keys) / sizeof(keys[0]); ++i) {
        EfErrCode rc = ef_del_env(keys[i]);
        if (rc != EF_NO_ERR && rc != EF_ENV_NAME_ERR) result = BK_FAIL;
    }
    /* Success retains the gate until reboot; late saves cannot repopulate it. */
    if (result != BK_OK) atomic_store(&s_storage_reset_pending, false);
    return result;
}

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
    return storage_write_blob(XIAOTAI_WIFI_KEY, &record, sizeof(record));
}

int xiaotai_storage_clear_wifi(void)
{
    return storage_delete(XIAOTAI_WIFI_KEY);
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
    return storage_write_blob(XIAOTAI_DEVICE_KEY, &record, sizeof(record));
}

int xiaotai_storage_clear_device(void)
{
    return storage_delete(XIAOTAI_DEVICE_KEY);
}

int xiaotai_storage_load_endpoint(char *output, size_t capacity)
{
    if (output == NULL || capacity < XIAOTAI_ENDPOINT_MAX) return BK_ERR_PARAM;
    output[0] = '\0';
    size_t saved = 0;
    size_t read = ef_get_env_blob(XIAOTAI_ENDPOINT_KEY, output,
                                  XIAOTAI_ENDPOINT_MAX, &saved);
    if (read == 0 || saved != read || read >= XIAOTAI_ENDPOINT_MAX ||
        output[read - 1] != '\0' || !endpoint_valid(output)) {
        output[0] = '\0';
        return BK_FAIL;
    }
    return BK_OK;
}

int xiaotai_storage_save_endpoint(const char *url)
{
    if (!endpoint_valid(url)) return BK_ERR_PARAM;
    return storage_write_blob(XIAOTAI_ENDPOINT_KEY, url, strlen(url) + 1U);
}

int xiaotai_storage_clear_endpoint(void)
{
    return storage_delete(XIAOTAI_ENDPOINT_KEY);
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
           value->microphone_sensitivity <= XIAOTAI_MIC_SENSITIVITY_MAX &&
           value->expression_index < 21U;
}

int xiaotai_storage_load_settings(xiaotai_product_settings_t *out)
{
    if (out == NULL) return BK_ERR_PARAM;
    xiaotai_settings_record_t record = {0};
    size_t saved = 0;
    size_t read = ef_get_env_blob(XIAOTAI_SETTINGS_KEY, &record,
                                  sizeof(record), &saved);
    /* v1 had five bytes, v2 added sensitivity. Their padded record sizes
     * equal v3; use payload_size and CRC, never reinterpret padding as fields. */
    size_t legacy_size = record.header.payload_size;
    if (read >= sizeof(record.header) + legacy_size &&
        saved == sizeof(record) &&
        record.header.magic == XIAOTAI_RECORD_MAGIC &&
        record.header.version == XIAOTAI_RECORD_VERSION &&
        (legacy_size == sizeof(xiaotai_product_settings_v1_t) || legacy_size == 6U) &&
        record.header.payload_crc == ef_calc_crc32(0, &record.payload, legacy_size)) {
        xiaotai_product_settings_default(out);
        memcpy(out, &record.payload, legacy_size);
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
    return storage_write_blob(XIAOTAI_SETTINGS_KEY, &record, sizeof(record));
}
