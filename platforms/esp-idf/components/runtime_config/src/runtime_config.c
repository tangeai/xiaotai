/*
 * TiRTC 设备凭证的 NVS adapter。
 *
 * 三个字段作为一组提交；读取任一必需字段失败时清空整个输出，避免调用者误用
 * 半组配置。这里使用普通 NVS，量产工程应由组合根启用 NVS 加密或安全芯片。
 */
#include "runtime_config.h"

#include <stdbool.h>
#include <stdio.h>
#include <string.h>

#include "nvs.h"
#include "esp_memory_utils.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"

#define TIRTC_NVS_NAMESPACE "tirtc_cfg"
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
    if (length >= RUNTIME_TIRTC_ENDPOINT_MAX || *host == '\0') return false;
    size_t host_length = 0;
    for (const char *p = host; *p != '\0'; ++p) {
        char c = *p;
        if (c == ':') {
            if (host_length == 0 || p[1] == '\0') return false;
            unsigned port = 0;
            for (++p; *p != '\0'; ++p) {
                if (*p < '0' || *p > '9') return false;
                port = port * 10U + (unsigned)(*p - '0');
                if (port > 65535U) return false;
            }
            if (port == 0U) return false;
            break;
        }
        if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
              (c >= '0' && c <= '9') || c == '.' || c == '-')) return false;
        ++host_length;
    }
    return host_length > 0;
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
    if (err != ESP_OK) {
        memset(config, 0, sizeof(*config));
    }
    return err;
}

esp_err_t runtime_config_save_tirtc(const runtime_tirtc_config_t *config)
{
    if (!runtime_config_tirtc_valid(config, NULL, 0)) {
        return ESP_ERR_INVALID_ARG;
    }
    nvs_handle_t nvs = 0;
    esp_err_t err = nvs_open(TIRTC_NVS_NAMESPACE, NVS_READWRITE, &nvs);
    if (err == ESP_OK) err = nvs_set_str(nvs, "device_id", config->device_id);
    if (err == ESP_OK) err = nvs_set_str(nvs, "secret", config->device_secret);
    if (err == ESP_OK) err = nvs_set_str(nvs, "client_id", config->client_id);
    if (err == ESP_OK) {
        esp_err_t erase_err = nvs_erase_key(nvs, "endpoint");
        if (erase_err != ESP_OK && erase_err != ESP_ERR_NVS_NOT_FOUND) {
            err = erase_err;
        }
    }
    /* nvs_commit 是整组配置对后续启动可见的提交点。 */
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

esp_err_t runtime_config_clear_tirtc(void)
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
