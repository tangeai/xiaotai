/*
 * ThingConnect 平台传输 adapter。
 *
 * 正常上线：服务发现 -> SNTP -> HMAC 设备登录 -> MQTT token -> 永久 MQTT。
 * 首次绑定：设备上报 -> 显示验证码 -> 临时 MQTT auth_grant -> QoS1 ACK。
 * 业务 HTTP：调用者复制请求到固定队列，由 request_task 串行执行和回调。
 *
 * 正常在线阶段的 MQTT token 只保存在本模块内存中。首次绑定会把设备密钥
 * 放入 provision result 交给组合根持久化；两者都不会写入日志。
 */
#include "platform_client.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "cJSON.h"
#include "esp_crt_bundle.h"
#include "esp_http_client.h"
#include "esp_log.h"
#include "esp_netif_sntp.h"
#include "esp_random.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "mbedtls/base64.h"
#include "mbedtls/md.h"
#include "mqtt_client.h"

#define PLATFORM_HTTP_BODY_MAX 8192
#define PLATFORM_REQUEST_QUEUE_DEPTH 4
#define PLATFORM_REQUEST_PATH_MAX 128
#define PLATFORM_REQUEST_BODY_MAX 2048
#define PLATFORM_SIGNAL_MAX 4096
#define PLATFORM_DEFAULT_HTTP_TIMEOUT_MS 15000U
#define PLATFORM_DEFAULT_DISCOVERY "http://ep-open.tangeopen.com/services"
#define PLATFORM_DEFAULT_PROVISION_TIMEOUT_SECONDS 190U
#define EXPERIENCE_PLATFORM_URL "https://xiaotai.chat"
#define PROVISION_DONE_BIT BIT0
#define PROVISION_ERROR_BIT BIT1

/* 服务发现结果；生成的起步工程会裁剪未使用的服务字段。 */
typedef struct {
    char device[256];
    char ai[256];
    char mqtt[256];
    char tirtc[256];
} platform_services_t;

typedef struct {
    char *data;
    size_t capacity;
    size_t length;
    bool overflow;
} http_output_t;

/* 请求按值进入固定队列，避免调用者栈内字符串在异步执行前失效。 */
typedef struct {
    platform_service_t service;
    char path[PLATFORM_REQUEST_PATH_MAX];
    bool post;
    char body[PLATFORM_REQUEST_BODY_MAX];
    unsigned timeout_ms;
    platform_response_callback_t callback;
    void *user_data;
} platform_request_t;

/* 首次绑定临时 MQTT 的全部可变状态，生命周期限制在 provision 调用内。 */
typedef struct {
    EventGroupHandle_t events;
    esp_mqtt_client_handle_t mqtt;
    char temp_client_id[65];
    char device_id[65];
    char device_secret[257];
    char message[PLATFORM_SIGNAL_MAX];
    size_t message_size;
    int message_id;
    int ack_message_id;
} provision_mqtt_t;

static const char *TAG = "platform_client";

/* 正常在线阶段的进程级状态；模块只支持一个设备实例。 */
static platform_services_t s_services;
static char s_device_id[65];
static char s_device_secret[257];
static char s_client_id[129];
static char s_mac_address[24];
static char s_mqtt_token[1024];
static QueueHandle_t s_request_queue;
static TaskHandle_t s_request_task;
static esp_mqtt_client_handle_t s_mqtt;
static volatile bool s_ready;
static volatile bool s_mqtt_connected;
static volatile bool s_provisioning;
static bool s_services_ready;
static bool s_sntp_initialized;
static char s_verification_code[17];
static platform_signal_callback_t s_signal_callback;
static void *s_signal_user_data;
static char s_mqtt_message[PLATFORM_SIGNAL_MAX];
static size_t s_mqtt_message_size;
static int s_mqtt_message_id = -1;

/* ===== 有界 HTTP 与 JSON 基础函数 ===== */

/* esp_http_client 可能分片回调；按容量拼接，溢出时整次请求失败。 */
static esp_err_t http_event(esp_http_client_event_t *event)
{
    if (event->event_id != HTTP_EVENT_ON_DATA || event->data == NULL ||
        event->data_len <= 0) {
        return ESP_OK;
    }
    http_output_t *output = event->user_data;
    if (output == NULL || output->overflow ||
        output->length + (size_t)event->data_len >= output->capacity) {
        if (output != NULL) {
            output->overflow = true;
        }
        return ESP_OK;
    }
    memcpy(output->data + output->length, event->data, (size_t)event->data_len);
    output->length += (size_t)event->data_len;
    output->data[output->length] = '\0';
    return ESP_OK;
}

static esp_err_t http_request(const char *url,
                              const char *json_body,
                              const char *bearer,
                              const char *const header_names[],
                              const char *const header_values[],
                              size_t header_count,
                              char *response,
                              size_t response_size,
                              unsigned timeout_ms,
                              int *status)
{
    /* response 由调用者提供，函数返回时总是以 NUL 结尾或报告容量错误。 */
    http_output_t output = {
        .data = response,
        .capacity = response_size,
    };
    response[0] = '\0';
    esp_http_client_config_t config = {
        .url = url,
        .event_handler = http_event,
        .user_data = &output,
        .timeout_ms = (int)(timeout_ms == 0
                                ? PLATFORM_DEFAULT_HTTP_TIMEOUT_MS
                                : timeout_ms),
        .crt_bundle_attach = esp_crt_bundle_attach,
        .disable_auto_redirect = false,
    };
    esp_http_client_handle_t client = esp_http_client_init(&config);
    if (client == NULL) {
        return ESP_ERR_NO_MEM;
    }
    if (json_body != NULL) {
        esp_http_client_set_method(client, HTTP_METHOD_POST);
        esp_http_client_set_header(client, "Content-Type", "application/json");
        esp_http_client_set_post_field(client, json_body, strlen(json_body));
    }
    char authorization[1100];
    if (bearer != NULL && bearer[0] != '\0') {
        int count = snprintf(authorization, sizeof(authorization), "Bearer %s", bearer);
        if (count <= 0 || (size_t)count >= sizeof(authorization)) {
            esp_http_client_cleanup(client);
            return ESP_ERR_INVALID_SIZE;
        }
        esp_http_client_set_header(client, "Authorization", authorization);
    }
    for (size_t i = 0; i < header_count; ++i) {
        esp_http_client_set_header(client, header_names[i], header_values[i]);
    }

    esp_err_t err = esp_http_client_perform(client);
    if (status != NULL) {
        *status = esp_http_client_get_status_code(client);
    }
    esp_http_client_cleanup(client);
    if (err == ESP_OK && output.overflow) {
        return ESP_ERR_INVALID_SIZE;
    }
    return err;
}

static bool json_copy_string(const cJSON *object,
                             const char *name,
                             char *destination,
                             size_t destination_size)
{
    const cJSON *item = cJSON_GetObjectItemCaseSensitive(object, name);
    if (!cJSON_IsString(item) || item->valuestring == NULL ||
        item->valuestring[0] == '\0' || strlen(item->valuestring) >= destination_size) {
        return false;
    }
    (void)snprintf(destination, destination_size, "%s", item->valuestring);
    return true;
}

static esp_err_t discover_services(const char *url)
{
    /* 服务地址属于运行时发现结果，不在固件中分别硬编码。 */
    char response[PLATFORM_HTTP_BODY_MAX];
    int status = 0;
    esp_err_t err = http_request(url, NULL, NULL, NULL, NULL, 0,
                                 response, sizeof(response),
                                 PLATFORM_DEFAULT_HTTP_TIMEOUT_MS, &status);
    if (err != ESP_OK || status != 200) {
        ESP_LOGE(TAG, "service discovery failed: %s HTTP=%d",
                 esp_err_to_name(err), status);
        return err == ESP_OK ? ESP_FAIL : err;
    }
    cJSON *root = cJSON_Parse(response);
    bool ok = root != NULL &&
              json_copy_string(root, "device-srv", s_services.device,
                               sizeof(s_services.device)) &&
              json_copy_string(root, "ai-srv", s_services.ai,
                               sizeof(s_services.ai)) &&
              json_copy_string(root, "mqtt-srv", s_services.mqtt,
                               sizeof(s_services.mqtt)) &&
              json_copy_string(root, "tirtc-srv", s_services.tirtc,
                               sizeof(s_services.tirtc));
    cJSON_Delete(root);
    if (!ok) {
        ESP_LOGE(TAG, "service discovery response is incomplete");
        return ESP_ERR_INVALID_RESPONSE;
    }
    s_services_ready = true;
    ESP_LOGI(TAG, "service discovery complete (credentials and token hidden)");
    return ESP_OK;
}

static esp_err_t hmac_signature_with_key(const char *key,
                                         const char *text,
                                         char *base64,
                                         size_t base64_size)
{
    unsigned char digest[32];
    const mbedtls_md_info_t *info = mbedtls_md_info_from_type(MBEDTLS_MD_SHA256);
    if (key == NULL || key[0] == '\0' || info == NULL ||
        mbedtls_md_hmac(info,
                        (const unsigned char *)key,
                        strlen(key),
                        (const unsigned char *)text,
                        strlen(text),
                        digest) != 0) {
        return ESP_FAIL;
    }
    size_t encoded = 0;
    if (mbedtls_base64_encode((unsigned char *)base64,
                              base64_size,
                              &encoded,
                              digest,
                              sizeof(digest)) != 0 || encoded >= base64_size) {
        return ESP_ERR_INVALID_SIZE;
    }
    base64[encoded] = '\0';
    return ESP_OK;
}

static esp_err_t hmac_signature(const char *text, char *base64, size_t base64_size)
{
    return hmac_signature_with_key(s_device_secret, text, base64, base64_size);
}

static esp_err_t obtain_mqtt_token(void)
{
    /* 时间戳、随机 nonce 和 HMAC 防止设备登录请求被直接重放。 */
    char timestamp[24];
    (void)snprintf(timestamp, sizeof(timestamp), "%lld", (long long)time(NULL));
    char nonce[17];
    (void)snprintf(nonce, sizeof(nonce), "%08lx%08lx",
                   (unsigned long)esp_random(), (unsigned long)esp_random());
    char signed_text[384];
    int text_length = snprintf(signed_text, sizeof(signed_text), "%s%s%s",
                               s_device_id, timestamp, nonce);
    char signature[64];
    if (text_length <= 0 || (size_t)text_length >= sizeof(signed_text) ||
        hmac_signature(signed_text, signature, sizeof(signature)) != ESP_OK) {
        return ESP_FAIL;
    }

    char url[384];
    int url_length = snprintf(url, sizeof(url), "%s/v1/device/token", s_services.device);
    if (url_length <= 0 || (size_t)url_length >= sizeof(url)) {
        return ESP_ERR_INVALID_SIZE;
    }
    const char *names[] = {
        "X-Device-Id", "X-Timestamp", "X-Nonce", "X-Mac", "X-Signature",
    };
    const char *values[] = {
        s_device_id, timestamp, nonce, s_mac_address, signature,
    };
    char response[PLATFORM_HTTP_BODY_MAX];
    int status = 0;
    esp_err_t err = http_request(url, "", NULL, names, values, 5,
                                 response, sizeof(response),
                                 PLATFORM_DEFAULT_HTTP_TIMEOUT_MS, &status);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "设备 token HTTP 请求失败: %s",
                 esp_err_to_name(err));
        return err;
    }
    cJSON *root = cJSON_Parse(response);
    const cJSON *code = root == NULL ? NULL : cJSON_GetObjectItemCaseSensitive(root, "code");
    const cJSON *data = root == NULL ? NULL : cJSON_GetObjectItemCaseSensitive(root, "data");
    bool ok = status == 200 && cJSON_IsNumber(code) && code->valueint == 200 &&
              cJSON_IsObject(data) &&
              json_copy_string(data, "mqtt_token", s_mqtt_token,
                               sizeof(s_mqtt_token));
    int response_code = cJSON_IsNumber(code) ? code->valueint : -1;
    char response_message[256] = "服务器未返回错误说明";
    if (root != NULL) {
        (void)json_copy_string(root, "msg", response_message,
                               sizeof(response_message));
    }
    cJSON_Delete(root);
    if (!ok) {
        if (response_code == 6006) {
            ESP_LOGW(TAG, "设备已被服务端解绑，需要重新进行验证码绑定");
            return ESP_ERR_NOT_FOUND;
        }
        ESP_LOGE(TAG, "设备 token 响应失败 HTTP=%d code=%d msg=%s",
                 status, response_code, response_message);
        return ESP_ERR_INVALID_RESPONSE;
    }
    ESP_LOGI(TAG, "device token obtained (value hidden)");
    return ESP_OK;
}

static const char *service_base(platform_service_t service)
{
    switch (service) {
    case PLATFORM_SERVICE_DEVICE: return s_services.device;
    case PLATFORM_SERVICE_AI: return s_services.ai;
    default: return NULL;
    }
}

static void process_request(const platform_request_t *request)
{
    /* 请求任务是业务 HTTP 的唯一执行者，callback 也在该任务中同步调用。 */
    const char *base = service_base(request->service);
    char url[512];
    int url_length = base == NULL ? -1 :
        snprintf(url, sizeof(url), "%s%s", base, request->path);
    char response[PLATFORM_HTTP_BODY_MAX];
    int status = 0;
    esp_err_t err = url_length <= 0 || (size_t)url_length >= sizeof(url)
                        ? ESP_ERR_INVALID_SIZE
                        : http_request(url,
                                       request->post ? request->body : NULL,
                                       s_mqtt_token,
                                       NULL,
                                       NULL,
                                       0,
                                       response,
                                       sizeof(response),
                                       request->timeout_ms,
                                       &status);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "platform request %s failed: %s HTTP=%d",
                 request->path, esp_err_to_name(err), status);
        if (request->callback != NULL) {
            request->callback(NULL, request->user_data);
        }
        return;
    }
    if (status < 200 || status >= 300) {
        ESP_LOGW(TAG, "platform request %s returned HTTP=%d",
                 request->path, status);
    }
    if (request->callback != NULL) {
        request->callback(response, request->user_data);
    }
}

static void publish_heartbeat(unsigned sequence)
{
    if (!s_mqtt_connected || s_mqtt == NULL) {
        return;
    }
    char topic[128];
    char body[128];
    (void)snprintf(topic, sizeof(topic), "device/sn_%s/up", s_device_id);
    int length = snprintf(body, sizeof(body),
                          "{\"type\":\"heartbeat\",\"seq\":%u,\"ts\":%lld}",
                          sequence, (long long)time(NULL));
    if (length > 0 && (size_t)length < sizeof(body)) {
        (void)esp_mqtt_client_publish(s_mqtt, topic, body, length, 0, 0);
    }
}

static void request_task(void *argument)
{
    /* 同一任务同时负责串行业务 HTTP 和每 30 秒心跳，避免额外后台任务。 */
    (void)argument;
    unsigned heartbeat_sequence = 0;
    int64_t next_heartbeat_ms = esp_timer_get_time() / 1000 + 30000;
    for (;;) {
        platform_request_t request;
        if (xQueueReceive(s_request_queue, &request, pdMS_TO_TICKS(1000)) == pdTRUE) {
            process_request(&request);
        }
        int64_t current_ms = esp_timer_get_time() / 1000;
        if (current_ms >= next_heartbeat_ms) {
            publish_heartbeat(++heartbeat_sequence);
            next_heartbeat_ms = current_ms + 30000;
        }
    }
}

static void dispatch_mqtt_signal(const char *json, size_t length)
{
    if (s_signal_callback != NULL) {
        s_signal_callback(json, length, s_signal_user_data);
    }
}

static void mqtt_event(void *handler_args,
                       esp_event_base_t base,
                       int32_t event_id,
                       void *event_data)
{
    /* MQTT payload 可能分片；只在完整且未超限时向上层分发。 */
    (void)handler_args;
    (void)base;
    esp_mqtt_event_handle_t event = event_data;
    if (event_id == MQTT_EVENT_CONNECTED) {
        char command_topic[128];
        char notify_topic[128];
        (void)snprintf(command_topic, sizeof(command_topic),
                       "device/sn_%s/cmd", s_device_id);
        (void)snprintf(notify_topic, sizeof(notify_topic),
                       "device/sn_%s/notify", s_device_id);
        (void)esp_mqtt_client_subscribe(s_mqtt, command_topic, 1);
        (void)esp_mqtt_client_subscribe(s_mqtt, notify_topic, 1);
        s_mqtt_connected = true;
        ESP_LOGI(TAG, "MQTT connected and device topics subscribed");
    } else if (event_id == MQTT_EVENT_DISCONNECTED) {
        s_mqtt_connected = false;
        ESP_LOGW(TAG, "MQTT disconnected; client will reconnect");
    } else if (event_id == MQTT_EVENT_DATA) {
        if (event->current_data_offset == 0) {
            s_mqtt_message_size = 0;
            s_mqtt_message_id = event->msg_id;
            if (event->topic != NULL && event->topic_len > 0) {
                char topic[160];
                size_t topic_length = (size_t)event->topic_len < sizeof(topic) - 1U
                                          ? (size_t)event->topic_len
                                          : sizeof(topic) - 1U;
                memcpy(topic, event->topic, topic_length);
                topic[topic_length] = '\0';
                if (strstr(topic, "/cmd") != NULL) {
                    char ack_topic[128];
                    (void)snprintf(ack_topic, sizeof(ack_topic),
                                   "device/sn_%s/ack", s_device_id);
                    (void)esp_mqtt_client_publish(s_mqtt,
                                                  ack_topic,
                                                  "{\"ack\":true}",
                                                  12,
                                                  1,
                                                  0);
                }
            }
        }
        bool valid_fragment = event->msg_id == s_mqtt_message_id &&
                              event->data_len >= 0 &&
                              s_mqtt_message_size + (size_t)event->data_len <
                                  sizeof(s_mqtt_message);
        if (!valid_fragment) {
            s_mqtt_message_id = -1;
            s_mqtt_message_size = 0;
            ESP_LOGW(TAG, "dropping oversized or fragmented MQTT signal");
            return;
        }
        memcpy(s_mqtt_message + s_mqtt_message_size,
               event->data,
               (size_t)event->data_len);
        s_mqtt_message_size += (size_t)event->data_len;
        if (s_mqtt_message_size == (size_t)event->total_data_len) {
            s_mqtt_message[s_mqtt_message_size] = '\0';
            dispatch_mqtt_signal(s_mqtt_message, s_mqtt_message_size);
            s_mqtt_message_id = -1;
            s_mqtt_message_size = 0;
        }
    } else if (event_id == MQTT_EVENT_ERROR) {
        ESP_LOGW(TAG, "MQTT transport error");
    }
}

static esp_err_t start_mqtt(void)
{
    esp_mqtt_client_config_t config = {
        .broker.address.uri = s_services.mqtt,
        .broker.verification.crt_bundle_attach = esp_crt_bundle_attach,
        .credentials.username = s_device_id,
        .credentials.client_id = s_client_id,
        .credentials.authentication.password = s_mqtt_token,
        .session.keepalive = 60,
        .session.protocol_ver = MQTT_PROTOCOL_V_3_1_1,
        .network.reconnect_timeout_ms = 3000,
    };
    s_mqtt = esp_mqtt_client_init(&config);
    if (s_mqtt == NULL) {
        return ESP_ERR_NO_MEM;
    }
    esp_err_t err = esp_mqtt_client_register_event(s_mqtt,
                                                   ESP_EVENT_ANY_ID,
                                                   mqtt_event,
                                                   NULL);
    if (err == ESP_OK) {
        err = esp_mqtt_client_start(s_mqtt);
    }
    if (err != ESP_OK) {
        (void)esp_mqtt_client_destroy(s_mqtt);
        s_mqtt = NULL;
    }
    return err;
}

static esp_err_t sync_clock(void)
{
    /* HMAC 登录依赖可信 Unix 时间；已同步时不重复初始化 SNTP。 */
    time_t current = time(NULL);
    if (current > 1700000000) {
        return ESP_OK;
    }
    if (!s_sntp_initialized) {
        esp_sntp_config_t config = ESP_NETIF_SNTP_DEFAULT_CONFIG("pool.ntp.org");
        esp_err_t init_err = esp_netif_sntp_init(&config);
        if (init_err != ESP_OK) {
            return init_err;
        }
        s_sntp_initialized = true;
    }
    esp_err_t err = ESP_OK;
    for (int attempt = 0; attempt < 5; ++attempt) {
        err = esp_netif_sntp_sync_wait(pdMS_TO_TICKS(2000));
        if (err == ESP_OK || time(NULL) > 1700000000) {
            ESP_LOGI(TAG, "network clock synchronized");
            return ESP_OK;
        }
    }
    ESP_LOGE(TAG, "network clock synchronization timed out");
    return ESP_ERR_TIMEOUT;
}

typedef struct {
    char code[17];
    char temp_token[1024];
    char temp_client_id[65];
} provision_report_t;

/* ===== 首次验证码绑定 / 服务端解绑后重绑 ===== */

static esp_err_t report_for_provision(const platform_provision_config_t *config,
                                      provision_report_t *report)
{
    /* existing_device_* 同时存在时给上报加签；首次绑定只上报 MAC。 */
    cJSON *body_root = cJSON_CreateObject();
    if (body_root == NULL ||
        !cJSON_AddStringToObject(body_root, "mac", config->mac_address)) {
        cJSON_Delete(body_root);
        return ESP_ERR_NO_MEM;
    }
    char *body = cJSON_PrintUnformatted(body_root);
    cJSON_Delete(body_root);
    if (body == NULL) {
        return ESP_ERR_NO_MEM;
    }

    const char *header_names[4] = {0};
    const char *header_values[4] = {0};
    size_t header_count = 0;
    char timestamp[24] = {0};
    char nonce[17] = {0};
    char signature[64] = {0};
    bool signed_report = config->existing_device_id != NULL &&
                         config->existing_device_id[0] != '\0' &&
                         config->existing_device_secret != NULL &&
                         config->existing_device_secret[0] != '\0';
    if (signed_report) {
        (void)snprintf(timestamp, sizeof(timestamp), "%lld", (long long)time(NULL));
        (void)snprintf(nonce, sizeof(nonce), "%08lx%08lx",
                       (unsigned long)esp_random(), (unsigned long)esp_random());
        char signed_text[384];
        int signed_length = snprintf(signed_text,
                                     sizeof(signed_text),
                                     "%s%s%s",
                                     config->existing_device_id,
                                     timestamp,
                                     nonce);
        if (signed_length <= 0 || (size_t)signed_length >= sizeof(signed_text) ||
            hmac_signature_with_key(config->existing_device_secret,
                                    signed_text,
                                    signature,
                                    sizeof(signature)) != ESP_OK) {
            free(body);
            return ESP_FAIL;
        }
        header_names[0] = "X-Device-Id";
        header_values[0] = config->existing_device_id;
        header_names[1] = "X-Timestamp";
        header_values[1] = timestamp;
        header_names[2] = "X-Nonce";
        header_values[2] = nonce;
        header_names[3] = "X-Signature";
        header_values[3] = signature;
        header_count = 4;
    }

    char url[384];
    int url_length = snprintf(url, sizeof(url), "%s/v1/device/report",
                              s_services.device);
    char response[PLATFORM_HTTP_BODY_MAX];
    int status = 0;
    esp_err_t err = url_length <= 0 || (size_t)url_length >= sizeof(url)
                        ? ESP_ERR_INVALID_SIZE
                        : http_request(url,
                                       body,
                                       NULL,
                                       header_names,
                                       header_values,
                                       header_count,
                                       response,
                                       sizeof(response),
                                       PLATFORM_DEFAULT_HTTP_TIMEOUT_MS,
                                       &status);
    free(body);
    if (err != ESP_OK || status != 200) {
        ESP_LOGE(TAG, "device report failed: %s HTTP=%d",
                 esp_err_to_name(err), status);
        return err == ESP_OK ? ESP_FAIL : err;
    }

    cJSON *root = cJSON_Parse(response);
    const cJSON *response_code = root == NULL
                                     ? NULL
                                     : cJSON_GetObjectItemCaseSensitive(root, "code");
    const cJSON *data = root == NULL
                            ? NULL
                            : cJSON_GetObjectItemCaseSensitive(root, "data");
    int code = cJSON_IsNumber(response_code) ? response_code->valueint : -1;
    bool ok = code == 200 && cJSON_IsObject(data) &&
              json_copy_string(data, "code", report->code, sizeof(report->code)) &&
              json_copy_string(data,
                               "temp_token",
                               report->temp_token,
                               sizeof(report->temp_token)) &&
              json_copy_string(data,
                               "temp_client_id",
                               report->temp_client_id,
                               sizeof(report->temp_client_id));
    cJSON_Delete(root);
    if (!ok) {
        if (code == 40901) {
            ESP_LOGW(TAG, "the previous verification code is still valid; retry later");
        } else {
            ESP_LOGE(TAG, "device report response rejected code=%d", code);
        }
        return ESP_ERR_INVALID_RESPONSE;
    }
    ESP_LOGI(TAG, "temporary MQTT credentials obtained (values hidden)");
    return ESP_OK;
}

static void provision_finish_with_error(provision_mqtt_t *context)
{
    if (context != NULL && context->events != NULL) {
        xEventGroupSetBits(context->events, PROVISION_ERROR_BIT);
    }
}

static void provision_handle_message(provision_mqtt_t *context,
                                     const char *json,
                                     size_t length)
{
    /* 只处理 auth_grant；凭证完整校验后才发送 QoS1 ACK。 */
    cJSON *root = cJSON_ParseWithLength(json, length);
    const cJSON *type = root == NULL
                            ? NULL
                            : cJSON_GetObjectItemCaseSensitive(root, "type");
    if (!cJSON_IsString(type) || strcmp(type->valuestring, "auth_grant") != 0) {
        cJSON_Delete(root);
        return;
    }

    const cJSON *payload = cJSON_GetObjectItemCaseSensitive(root, "payload");
    if (cJSON_IsObject(payload)) {
        const cJSON *device_id = cJSON_GetObjectItemCaseSensitive(payload, "device_id");
        const cJSON *device_key = cJSON_GetObjectItemCaseSensitive(payload, "device_key");
        bool has_id = cJSON_IsString(device_id) && device_id->valuestring != NULL &&
                      device_id->valuestring[0] != '\0';
        bool has_key = cJSON_IsString(device_key) && device_key->valuestring != NULL &&
                       device_key->valuestring[0] != '\0';
        if (has_id != has_key ||
            (has_id && (strlen(device_id->valuestring) >= sizeof(context->device_id) ||
                        strlen(device_key->valuestring) >=
                            sizeof(context->device_secret)))) {
            ESP_LOGE(TAG, "auth_grant contains invalid device credentials");
            cJSON_Delete(root);
            provision_finish_with_error(context);
            return;
        }
        if (has_id) {
            (void)snprintf(context->device_id,
                           sizeof(context->device_id),
                           "%s",
                           device_id->valuestring);
            (void)snprintf(context->device_secret,
                           sizeof(context->device_secret),
                           "%s",
                           device_key->valuestring);
        }
    }
    cJSON_Delete(root);

    if (context->device_id[0] == '\0' || context->device_secret[0] == '\0') {
        ESP_LOGE(TAG, "auth_grant did not provide credentials");
        provision_finish_with_error(context);
        return;
    }
    char ack_topic[128];
    (void)snprintf(ack_topic,
                   sizeof(ack_topic),
                   "device/%s/ack",
                   context->temp_client_id);
    context->ack_message_id = esp_mqtt_client_publish(context->mqtt,
                                                       ack_topic,
                                                       "{\"ack\":true}",
                                                       12,
                                                       1,
                                                       0);
    if (context->ack_message_id < 0) {
        ESP_LOGE(TAG, "cannot publish auth_grant ACK");
        provision_finish_with_error(context);
    }
}

static void provision_mqtt_event(void *handler_args,
                                 esp_event_base_t base,
                                 int32_t event_id,
                                 void *event_data)
{
    /* context 位于阻塞等待函数的栈上，MQTT 停止并销毁后才会离开作用域。 */
    (void)base;
    provision_mqtt_t *context = handler_args;
    esp_mqtt_event_handle_t event = event_data;
    if (event_id == MQTT_EVENT_CONNECTED) {
        char topic[128];
        (void)snprintf(topic,
                       sizeof(topic),
                       "device/%s/cmd",
                       context->temp_client_id);
        if (esp_mqtt_client_subscribe(context->mqtt, topic, 1) < 0) {
            ESP_LOGE(TAG, "cannot subscribe temporary binding topic");
            provision_finish_with_error(context);
        } else {
            ESP_LOGI(TAG, "temporary MQTT connected; waiting for H5 binding");
        }
    } else if (event_id == MQTT_EVENT_DATA) {
        if (event->current_data_offset == 0) {
            context->message_size = 0;
            context->message_id = event->msg_id;
        }
        bool valid = event->msg_id == context->message_id && event->data_len >= 0 &&
                     context->message_size + (size_t)event->data_len <
                         sizeof(context->message);
        if (!valid) {
            ESP_LOGW(TAG, "dropping oversized temporary MQTT message");
            context->message_id = -1;
            context->message_size = 0;
            return;
        }
        memcpy(context->message + context->message_size,
               event->data,
               (size_t)event->data_len);
        context->message_size += (size_t)event->data_len;
        if (context->message_size == (size_t)event->total_data_len) {
            context->message[context->message_size] = '\0';
            provision_handle_message(context,
                                     context->message,
                                     context->message_size);
            context->message_id = -1;
            context->message_size = 0;
        }
    } else if (event_id == MQTT_EVENT_PUBLISHED &&
               event->msg_id == context->ack_message_id) {
        ESP_LOGI(TAG, "binding ACK delivered");
        xEventGroupSetBits(context->events, PROVISION_DONE_BIT);
    } else if (event_id == MQTT_EVENT_ERROR) {
        ESP_LOGW(TAG,
                 "temporary MQTT transport error; waiting for automatic reconnect");
    }
}

static esp_err_t wait_for_auth_grant(const provision_report_t *report,
                                     const platform_provision_config_t *config,
                                     platform_provision_result_t *result)
{
    /* 临时 MQTT 与正常设备 MQTT 完全分离，完成或超时后始终销毁。 */
    provision_mqtt_t context = {
        .message_id = -1,
        .ack_message_id = -1,
    };
    (void)snprintf(context.temp_client_id,
                   sizeof(context.temp_client_id),
                   "%s",
                   report->temp_client_id);
    if (config->existing_device_id != NULL &&
        config->existing_device_secret != NULL) {
        (void)snprintf(context.device_id,
                       sizeof(context.device_id),
                       "%s",
                       config->existing_device_id);
        (void)snprintf(context.device_secret,
                       sizeof(context.device_secret),
                       "%s",
                       config->existing_device_secret);
    }
    context.events = xEventGroupCreate();
    if (context.events == NULL) {
        return ESP_ERR_NO_MEM;
    }
    esp_mqtt_client_config_t mqtt_config = {
        .broker.address.uri = s_services.mqtt,
        .broker.verification.crt_bundle_attach = esp_crt_bundle_attach,
        .credentials.username = report->temp_client_id,
        .credentials.client_id = report->temp_client_id,
        .credentials.authentication.password = report->temp_token,
        .session.keepalive = 60,
        .session.protocol_ver = MQTT_PROTOCOL_V_3_1_1,
        .network.reconnect_timeout_ms = 3000,
    };
    context.mqtt = esp_mqtt_client_init(&mqtt_config);
    if (context.mqtt == NULL) {
        vEventGroupDelete(context.events);
        return ESP_ERR_NO_MEM;
    }
    esp_err_t err = esp_mqtt_client_register_event(context.mqtt,
                                                    ESP_EVENT_ANY_ID,
                                                    provision_mqtt_event,
                                                    &context);
    if (err == ESP_OK) {
        err = esp_mqtt_client_start(context.mqtt);
    }
    if (err != ESP_OK) {
        esp_mqtt_client_destroy(context.mqtt);
        vEventGroupDelete(context.events);
        return err;
    }

    unsigned timeout_seconds = config->timeout_seconds == 0
                                   ? PLATFORM_DEFAULT_PROVISION_TIMEOUT_SECONDS
                                   : config->timeout_seconds;
    EventBits_t bits = xEventGroupWaitBits(context.events,
                                           PROVISION_DONE_BIT | PROVISION_ERROR_BIT,
                                           pdFALSE,
                                           pdFALSE,
                                           pdMS_TO_TICKS(timeout_seconds * 1000U));
    (void)esp_mqtt_client_stop(context.mqtt);
    (void)esp_mqtt_client_destroy(context.mqtt);
    vEventGroupDelete(context.events);
    if ((bits & PROVISION_DONE_BIT) == 0) {
        if ((bits & PROVISION_ERROR_BIT) != 0) {
            ESP_LOGE(TAG, "verification binding failed");
            return ESP_FAIL;
        }
        ESP_LOGE(TAG,
                 "verification binding timed out after %u seconds",
                 timeout_seconds);
        return ESP_ERR_TIMEOUT;
    }
    (void)snprintf(result->device_id,
                   sizeof(result->device_id),
                   "%s",
                   context.device_id);
    (void)snprintf(result->device_secret,
                   sizeof(result->device_secret),
                   "%s",
                   context.device_secret);
    return ESP_OK;
}

esp_err_t platform_client_provision(const platform_provision_config_t *config,
                                    platform_provision_result_t *result)
{
    /* 这是同步编排入口；调用者负责把 result 安全持久化。 */
    if (config == NULL || result == NULL || config->mac_address == NULL ||
        config->mac_address[0] == '\0' ||
        strlen(config->mac_address) >= sizeof(s_mac_address) ||
        ((config->existing_device_id == NULL) !=
         (config->existing_device_secret == NULL)) ||
        (config->existing_device_id != NULL &&
         (strlen(config->existing_device_id) >= sizeof(result->device_id) ||
          strlen(config->existing_device_secret) >= sizeof(result->device_secret)))) {
        return ESP_ERR_INVALID_ARG;
    }
    memset(result, 0, sizeof(*result));
    esp_err_t err = sync_clock();
    if (err != ESP_OK) {
        return err;
    }
    if (!s_services_ready) {
        const char *discovery = config->discovery_url != NULL &&
                                config->discovery_url[0] != '\0'
                                    ? config->discovery_url
                                    : PLATFORM_DEFAULT_DISCOVERY;
        err = discover_services(discovery);
        if (err != ESP_OK) {
            return err;
        }
    }
    provision_report_t report = {0};
    err = report_for_provision(config, &report);
    if (err != ESP_OK) {
        return err;
    }
    ESP_LOGW(TAG, "============================================================");
    ESP_LOGW(TAG, "verification code: %s", report.code);
    ESP_LOGW(TAG, "registration/login: %s", EXPERIENCE_PLATFORM_URL);
    ESP_LOGW(TAG, "open device binding and enter this verification code");
    ESP_LOGW(TAG, "============================================================");
    (void)snprintf(s_verification_code,
                   sizeof(s_verification_code),
                   "%s",
                   report.code);
    s_provisioning = true;
    if (config->on_verification_code != NULL) {
        config->on_verification_code(s_verification_code,
                                     config->verification_code_user_data);
    }
    err = wait_for_auth_grant(&report, config, result);
    s_provisioning = false;
    s_verification_code[0] = '\0';
    if (err == ESP_OK) {
        ESP_LOGI(TAG, "verification binding completed; credentials ready for NVS");
    }
    return err;
}

esp_err_t platform_client_start(const platform_client_config_t *config)
{
    /* 正常在线入口只创建一次永久请求任务和 MQTT client。 */
    if (s_ready) {
        return ESP_OK;
    }
    if (config == NULL || config->device_id == NULL || config->device_secret == NULL ||
        config->mac_address == NULL ||
        config->device_id[0] == '\0' || config->device_secret[0] == '\0' ||
        strlen(config->device_id) >= sizeof(s_device_id) ||
        strlen(config->device_secret) >= sizeof(s_device_secret) ||
        strlen(config->mac_address) >= sizeof(s_mac_address)) {
        return ESP_ERR_INVALID_ARG;
    }
    (void)snprintf(s_device_id, sizeof(s_device_id), "%s", config->device_id);
    (void)snprintf(s_device_secret, sizeof(s_device_secret), "%s", config->device_secret);
    int client_length = snprintf(s_client_id, sizeof(s_client_id),
                                 "sn_%s", config->device_id);
    if (client_length <= 0 || (size_t)client_length >= sizeof(s_client_id)) {
        return ESP_ERR_INVALID_SIZE;
    }
    (void)snprintf(s_mac_address, sizeof(s_mac_address), "%s", config->mac_address);

    esp_err_t err = sync_clock();
    if (err != ESP_OK) {
        return err;
    }
    const char *discovery = config->discovery_url != NULL &&
                            config->discovery_url[0] != '\0'
                                ? config->discovery_url
                                : PLATFORM_DEFAULT_DISCOVERY;
    if (!s_services_ready) {
        err = discover_services(discovery);
        if (err != ESP_OK) {
            return err;
        }
    }
    err = obtain_mqtt_token();
    if (err != ESP_OK) {
        return err;
    }
    s_request_queue = xQueueCreate(PLATFORM_REQUEST_QUEUE_DEPTH,
                                   sizeof(platform_request_t));
    if (s_request_queue == NULL) {
        return ESP_ERR_NO_MEM;
    }
    if (xTaskCreate(request_task, "platform_http", 24576, NULL, 5,
                    &s_request_task) != pdPASS) {
        vQueueDelete(s_request_queue);
        s_request_queue = NULL;
        return ESP_ERR_NO_MEM;
    }
    err = start_mqtt();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "MQTT start failed: %s", esp_err_to_name(err));
        if (s_request_task != NULL) {
            vTaskDelete(s_request_task);
            s_request_task = NULL;
        }
        if (s_request_queue != NULL) {
            vQueueDelete(s_request_queue);
            s_request_queue = NULL;
        }
        return err;
    }
    s_ready = true;
    return ESP_OK;
}

bool platform_client_ready(void)
{
    return s_ready;
}

bool platform_client_mqtt_connected(void)
{
    return s_mqtt_connected;
}

bool platform_client_provisioning(void)
{
    return s_provisioning;
}

const char *platform_client_verification_code(void)
{
    return s_verification_code;
}

const char *platform_client_tirtc_endpoint(void)
{
    return s_ready && s_services.tirtc[0] != '\0' ? s_services.tirtc : NULL;
}

esp_err_t platform_client_request(platform_service_t service,
                                  const char *path,
                                  const char *json_body,
                                  platform_response_callback_t callback,
                                  void *user_data)
{
    return platform_client_request_timeout(service,
                                           path,
                                           json_body,
                                           PLATFORM_DEFAULT_HTTP_TIMEOUT_MS,
                                           callback,
                                           user_data);
}

esp_err_t platform_client_request_timeout(platform_service_t service,
                                          const char *path,
                                          const char *json_body,
                                          unsigned timeout_ms,
                                          platform_response_callback_t callback,
                                          void *user_data)
{
    if (!s_ready || s_request_queue == NULL) {
        return ESP_ERR_INVALID_STATE;
    }
    if (service_base(service) == NULL || path == NULL || path[0] != '/' ||
        strlen(path) >= PLATFORM_REQUEST_PATH_MAX ||
        (json_body != NULL && strlen(json_body) >= PLATFORM_REQUEST_BODY_MAX) ||
        timeout_ms == 0 || timeout_ms > 60000U) {
        return ESP_ERR_INVALID_ARG;
    }
    /* path/body 复制进队列项；callback/user_data 的生命周期由调用者保证。 */
    platform_request_t request = {
        .service = service,
        .post = json_body != NULL,
        .timeout_ms = timeout_ms,
        .callback = callback,
        .user_data = user_data,
    };
    (void)snprintf(request.path, sizeof(request.path), "%s", path);
    if (json_body != NULL) {
        (void)snprintf(request.body, sizeof(request.body), "%s", json_body);
    }
    return xQueueSend(s_request_queue, &request, 0) == pdTRUE
               ? ESP_OK
               : ESP_ERR_TIMEOUT;
}

void platform_client_set_signal_handler(platform_signal_callback_t callback,
                                        void *user_data)
{
    s_signal_callback = callback;
    s_signal_user_data = user_data;
}
