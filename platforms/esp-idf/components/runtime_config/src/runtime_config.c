/*
 * TiRTC 设备凭证的 NVS adapter。
 *
 * 版本化 blob 保存整组身份；兼容读取旧的分键格式，读取失败时清空输出。这里使用普通 NVS，量产工程应由组合根启用 NVS 加密或安全芯片。
 */
#include "runtime_config.h"

#include <stdbool.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include "xiaotai_ntp_config.h"
#include "xiaotai_identity_gate.h"

#include "nvs.h"
#include "esp_memory_utils.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"

/* Identity writes are serialized without holding a Flash/RTOS critical section. */
static xiaotai_identity_gate_t s_identity_gate;

#define TIRTC_NVS_NAMESPACE "tirtc_cfg"
#define TIRTC_IDENTITY_KEY "identity_v1"
typedef struct {
    uint32_t version;
    runtime_tirtc_config_t config;
} runtime_identity_record_t;
#define TIRTC_ENDPOINT_NVS_NAMESPACE "tirtc_ep"
#define TIRTC_ENDPOINT_NVS_KEY "url"
#ifndef RUNTIME_TIRTC_ENDPOINT_MAX
#define RUNTIME_TIRTC_ENDPOINT_MAX 256
#endif

bool runtime_config_endpoint_valid(const char *url)
{
    if (url == NULL) return false;
    const char *host = NULL;
    if (strncmp(url, "http://", 7) == 0) host = url + 7;
    else if (strncmp(url, "https://", 8) == 0) host = url + 8;
    else return false;
    size_t length = 0;
    while (length < RUNTIME_TIRTC_ENDPOINT_MAX && url[length] != '\0') ++length;
    if (length >= RUNTIME_TIRTC_ENDPOINT_MAX - sizeof("/services") || *host == '\0') return false;
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

bool runtime_config_format_discovery_url(const char *base, char *output, size_t capacity)
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

esp_err_t runtime_config_load_endpoint(char *output, size_t capacity)
{
    if (output == NULL || capacity == 0) return ESP_ERR_INVALID_ARG;
    output[0] = '\0';
    nvs_handle_t nvs = 0;
    esp_err_t err = nvs_open(TIRTC_ENDPOINT_NVS_NAMESPACE, NVS_READONLY, &nvs);
    if (err != ESP_OK) return err;
    size_t size = capacity;
    err = nvs_get_str(nvs, TIRTC_ENDPOINT_NVS_KEY, output, &size);
    nvs_close(nvs);
    if (err != ESP_OK || !runtime_config_endpoint_valid(output)) {
        output[0] = '\0';
        return err == ESP_OK ? ESP_ERR_INVALID_ARG : err;
    }
    return ESP_OK;
}

esp_err_t runtime_config_save_endpoint(const char *url)
{
    if (!runtime_config_endpoint_valid(url)) return ESP_ERR_INVALID_ARG;
    nvs_handle_t nvs = 0;
    esp_err_t err = nvs_open(TIRTC_ENDPOINT_NVS_NAMESPACE, NVS_READWRITE, &nvs);
    if (err == ESP_OK) err = nvs_set_str(nvs, TIRTC_ENDPOINT_NVS_KEY, url);
    if (err == ESP_OK) err = nvs_commit(nvs);
    if (nvs != 0) nvs_close(nvs);
    return err;
}

esp_err_t runtime_config_clear_endpoint(void)
{
    nvs_handle_t nvs = 0;
    esp_err_t err = nvs_open(TIRTC_ENDPOINT_NVS_NAMESPACE, NVS_READWRITE, &nvs);
    if (err == ESP_OK) err = nvs_erase_key(nvs, TIRTC_ENDPOINT_NVS_KEY);
    if (err == ESP_ERR_NVS_NOT_FOUND) err = ESP_OK;
    if (err == ESP_OK) err = nvs_commit(nvs);
    if (nvs != 0) nvs_close(nvs);
    return err;
}

static void set_error(char *error, size_t error_size, const char *message)
{
    if (error != NULL && error_size > 0) {
        (void)snprintf(error, error_size, "%s", message);
    }
}

bool runtime_config_tirtc_valid(const runtime_tirtc_config_t *config,
                                char *error,
                                size_t error_size)
{
    if (config == NULL) {
        set_error(error, error_size, "config is null");
        return false;
    }
    if (memchr(config->device_id, '\0', sizeof(config->device_id)) == NULL ||
        memchr(config->device_secret, '\0', sizeof(config->device_secret)) == NULL ||
        memchr(config->client_id, '\0', sizeof(config->client_id)) == NULL) {
        set_error(error, error_size, "unterminated credential field");
        return false;
    }
    size_t device_id_length = strlen(config->device_id);
    size_t secret_length = strlen(config->device_secret);
    if (device_id_length == 0 || device_id_length >= sizeof(config->device_id)) {
        set_error(error, error_size, "device_id length must be 1..64 bytes");
        return false;
    }
    if (secret_length == 0 || secret_length >= sizeof(config->device_secret)) {
        set_error(error, error_size, "device_secret length must be 1..256 bytes");
        return false;
    }
    set_error(error, error_size, "");
    return true;
}

esp_err_t runtime_config_load_tirtc(runtime_tirtc_config_t *config)
{
    if (config == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    memset(config, 0, sizeof(*config));
    nvs_handle_t nvs = 0;
    esp_err_t err = nvs_open(TIRTC_NVS_NAMESPACE, NVS_READONLY, &nvs);
    if (err != ESP_OK) {
        return err;
    }
    runtime_identity_record_t record = {0};
    size_t record_size = sizeof(record);
    err = nvs_get_blob(nvs, TIRTC_IDENTITY_KEY, &record, &record_size);
    if (err != ESP_ERR_NVS_NOT_FOUND) {
        nvs_close(nvs);
        if (err != ESP_OK) return err;
        if (record_size != sizeof(record) || record.version != 1 ||
            !runtime_config_tirtc_valid(&record.config, NULL, 0)) return ESP_ERR_INVALID_ARG;
        *config = record.config;
        return ESP_OK;
    }
    /* Legacy credentials are read only when no versioned record exists.
     * Never revive an old identity after detecting a damaged new record. */
    /* client_id 是向后兼容的可选字段；ID 和 secret 必须同时存在。 */
    size_t size = sizeof(config->device_id);
    err = nvs_get_str(nvs, "device_id", config->device_id, &size);
    if (err == ESP_OK) {
        size = sizeof(config->device_secret);
        err = nvs_get_str(nvs, "secret", config->device_secret, &size);
    }
    if (err == ESP_OK) {
        size = sizeof(config->client_id);
        err = nvs_get_str(nvs, "client_id", config->client_id, &size);
        if (err == ESP_ERR_NVS_NOT_FOUND) {
            config->client_id[0] = '\0';
            err = ESP_OK;
        }
    }
    nvs_close(nvs);
    if (err == ESP_OK && !runtime_config_tirtc_valid(config, NULL, 0)) err = ESP_ERR_INVALID_ARG;
    if (err != ESP_OK) {
        memset(config, 0, sizeof(*config));
    }
    return err;
}

static esp_err_t save_tirtc_unlocked(const runtime_tirtc_config_t *config)
{
    if (!runtime_config_tirtc_valid(config, NULL, 0)) {
        return ESP_ERR_INVALID_ARG;
    }
    nvs_handle_t nvs = 0;
    esp_err_t err = nvs_open(TIRTC_NVS_NAMESPACE, NVS_READWRITE, &nvs);
    /* First preserve the currently readable identity as a complete blob.
     * Only then retire legacy keys, before publishing a different identity.
     * If NVS later discards a damaged blob, no stale legacy identity can revive.
     * Migration retires legacy fields only after preserving a readable old
     * identity; it never updates individual credential fields. */
    runtime_identity_record_t record = {.version = 1};
    if (err == ESP_OK) {
        esp_err_t load_err = runtime_config_load_tirtc(&record.config);
        if (load_err == ESP_OK) {
            err = nvs_set_blob(nvs, TIRTC_IDENTITY_KEY, &record, sizeof(record));
            if (err == ESP_OK) err = nvs_commit(nvs);
        } else if (load_err != ESP_ERR_NVS_NOT_FOUND && load_err != ESP_ERR_INVALID_ARG) {
            err = load_err;
        }
    }
    static const char *const legacy_keys[] = {"device_id", "secret", "client_id", "endpoint"};
    for (size_t i = 0; err == ESP_OK && i < sizeof(legacy_keys)/sizeof(legacy_keys[0]); ++i) {
        esp_err_t erase_err = nvs_erase_key(nvs, legacy_keys[i]);
        if (erase_err != ESP_OK && erase_err != ESP_ERR_NVS_NOT_FOUND) err = erase_err;
    }
    if (err == ESP_OK) err = nvs_commit(nvs);
    /* NVS set_str writes immediately; commit is not a multi-key transaction.
     * Publish the new ID, secret and client ID with one blob update. */
    record.config = *config;
    if (err == ESP_OK) err = nvs_set_blob(nvs, TIRTC_IDENTITY_KEY, &record, sizeof(record));
    if (err == ESP_OK) err = nvs_commit(nvs);
    if (nvs != 0) nvs_close(nvs);
    return err;
}

static esp_err_t clear_tirtc_on_internal_stack(void)
{
    nvs_handle_t nvs = 0;
    esp_err_t err = nvs_open(TIRTC_NVS_NAMESPACE, NVS_READWRITE, &nvs);
    if (err == ESP_OK) err = nvs_erase_all(nvs);
    if (err == ESP_OK) err = nvs_commit(nvs);
    if (nvs != 0) nvs_close(nvs);
    return err;
}


typedef struct {
    SemaphoreHandle_t done;
    esp_err_t result;
} clear_tirtc_request_t;

static void clear_tirtc_task(void *argument)
{
    clear_tirtc_request_t *request = argument;
    /* NVS may disable the PSRAM cache. Only this ordinary FreeRTOS task's
     * internal stack is used during erase/commit. Access the caller's request
     * again only after NVS has restored the cache. */
    esp_err_t result = clear_tirtc_on_internal_stack();
    SemaphoreHandle_t done = request->done;
    request->result = result;
    xSemaphoreGive(done);
    /* Do not access request after signalling: the caller may have returned.
     * Ordinary xTaskCreate/vTaskDelete is an intentional allocation pair. */
    vTaskDelete(NULL);
}

static esp_err_t clear_tirtc_unlocked(void)
{
    unsigned stack_probe = 0;
    if (esp_ptr_internal(&stack_probe)) {
        return clear_tirtc_on_internal_stack();
    }
    clear_tirtc_request_t request = {.result = ESP_FAIL};
    request.done = xSemaphoreCreateBinary();
    if (request.done == NULL) return ESP_ERR_NO_MEM;
    /* ESP-IDF's ordinary xTaskCreate allocates stack/TCB from internal RAM.
     * Keep the 24 KiB session task in PSRAM; allocate this 6 KiB worker only
     * for erase, then synchronously return its real NVS result. */
    if (xTaskCreate(clear_tirtc_task, "clear_tirtc", 6144, &request,
                    uxTaskPriorityGet(NULL), NULL) != pdPASS) {
        vSemaphoreDelete(request.done);
        return ESP_ERR_NO_MEM;
    }
    xSemaphoreTake(request.done, portMAX_DELAY);
    vSemaphoreDelete(request.done);
    return request.result;
}

#define NTP_NVS_NAMESPACE "ntp_cfg"
#define NTP_NVS_KEY "server"

esp_err_t runtime_config_load_ntp_server(char *output, size_t capacity)
{
    if (output == NULL || capacity == 0) return ESP_ERR_INVALID_ARG;
    output[0] = '\0';
    nvs_handle_t nvs = 0;
    esp_err_t err = nvs_open(NTP_NVS_NAMESPACE, NVS_READONLY, &nvs);
    if (err != ESP_OK) return err;
    size_t size = capacity;
    err = nvs_get_str(nvs, NTP_NVS_KEY, output, &size);
    nvs_close(nvs);
    if (err != ESP_OK || !xiaotai_ntp_server_valid(output)) {
        output[0] = '\0';
        return err == ESP_OK ? ESP_ERR_INVALID_ARG : err;
    }
    return ESP_OK;
}

esp_err_t runtime_config_save_ntp_server(const char *url)
{
    if (!xiaotai_ntp_server_valid(url)) return ESP_ERR_INVALID_ARG;
    nvs_handle_t nvs = 0;
    esp_err_t err = nvs_open(NTP_NVS_NAMESPACE, NVS_READWRITE, &nvs);
    if (err == ESP_OK) err = nvs_set_str(nvs, NTP_NVS_KEY, url);
    if (err == ESP_OK) err = nvs_commit(nvs);
    if (nvs != 0) nvs_close(nvs);
    return err;
}

esp_err_t runtime_config_clear_ntp_server(void)
{
    nvs_handle_t nvs = 0;
    esp_err_t err = nvs_open(NTP_NVS_NAMESPACE, NVS_READWRITE, &nvs);
    if (err == ESP_OK) err = nvs_erase_key(nvs, NTP_NVS_KEY);
    if (err == ESP_ERR_NVS_NOT_FOUND) err = ESP_OK;
    if (err == ESP_OK) err = nvs_commit(nvs);
    if (nvs != 0) nvs_close(nvs);
    return err;
}

esp_err_t runtime_config_save_tirtc(const runtime_tirtc_config_t *value)
{
    if (!xiaotai_identity_write_begin(&s_identity_gate)) return ESP_ERR_INVALID_STATE;
    esp_err_t result = save_tirtc_unlocked(value);
    xiaotai_identity_write_end(&s_identity_gate);
    return result;
}

esp_err_t runtime_config_clear_tirtc(void)
{
    if (!xiaotai_identity_write_begin(&s_identity_gate)) return ESP_ERR_INVALID_STATE;
    esp_err_t result = clear_tirtc_unlocked();
    xiaotai_identity_write_end(&s_identity_gate);
    return result;
}

esp_err_t runtime_config_recover_tirtc(const runtime_tirtc_config_t *value)
{
    int previous = xiaotai_identity_recovery_begin(&s_identity_gate);
    if (previous < 0) return ESP_ERR_INVALID_STATE;
    esp_err_t result = value != NULL ? save_tirtc_unlocked(value) : clear_tirtc_unlocked();
    xiaotai_identity_recovery_end(&s_identity_gate, previous, result == ESP_OK);
    return result;
}
