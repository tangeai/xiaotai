/*
 * 工程组合根：只负责初始化基础设施、组装模块和持有启动任务。
 *
 * 启动顺序：NVS -> ATK 板级媒体/按键 -> Wi-Fi -> 串口控制台 -> 后台上线任务。
 * 后台任务等待网络后完成绑定、平台注册和 TiRTC 启动。H5/AI 业务顺序由
 * starter_runtime 隐藏，app_main 不直接处理 SDK 回调或音视频帧。
 */
#include <inttypes.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>

#include "esp_chip_info.h"
#include "esp_heap_caps.h"
#include "esp_idf_version.h"
#include "esp_log.h"
#include "esp_mac.h"
#include "esp_system.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "nvs_flash.h"
#include "platform_client.h"
#include "runtime_config.h"
#include "starter_console.h"
#include "starter_media.h"
#include "starter_runtime.h"
#include "starter_tirtc.h"
#include "wifi_manager.h"

#define DISCOVERY_URL "https://ep-open.tangeopen.com/services"
#define START_RETRY_DELAY_MS 5000U

static const char *TAG = "starter_main";
static runtime_tirtc_config_t s_tirtc_config;
static QueueHandle_t s_control_queue;

static void on_control_intent(starter_media_control_intent_t intent, void *user_data)
{
    (void)user_data;
    if (s_control_queue == NULL || xQueueSend(s_control_queue, &intent, 0) != pdTRUE) {
        ESP_LOGE(TAG, "physical control intent dropped: %d", (int)intent);
    }
}

static void control_task(void *argument)
{
    (void)argument;
    starter_media_control_intent_t intent;
    for (;;) {
        if (xQueueReceive(s_control_queue, &intent, portMAX_DELAY) != pdTRUE) continue;
        esp_err_t err = ESP_ERR_INVALID_ARG;
        if (intent == STARTER_MEDIA_CONTROL_WIFI_RESET) {
            err = wifi_manager_forget_credentials();
            ESP_LOGW(TAG, "KEY2 Wi-Fi reset: %s", esp_err_to_name(err));
        } else if (intent == STARTER_MEDIA_CONTROL_BINDING_RESET) {
            err = runtime_config_clear_tirtc();
            ESP_LOGW(TAG, "KEY3 binding reset: %s", esp_err_to_name(err));
        }
        if (err == ESP_OK) {
            vTaskDelay(pdMS_TO_TICKS(300));
            esp_restart();
        }
    }
}

static void on_verification_code(const char *code, void *user_data)
{
    (void)user_data;
    esp_err_t err = starter_media_announce_binding_code(code);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "binding voice unavailable: %s", esp_err_to_name(err));
    }
}

/* 使用 STA MAC 生成稳定的设备指纹和默认 TiRTC client_id。 */
static void station_identity(char mac_address[18], char client_id[65])
{
    uint8_t mac[6];
    esp_read_mac(mac, ESP_MAC_WIFI_STA);
    (void)snprintf(mac_address,
                   18,
                   "%02X:%02X:%02X:%02X:%02X:%02X",
                   mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
    (void)snprintf(client_id,
                   65,
                   "esp32s3-%02x%02x%02x%02x%02x%02x",
                   mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
}

static esp_err_t provision_and_save(const char *mac_address, bool signed_rebind)
{
    /* TODO(product-security): enable encrypted NVS or a secure element before
     * storing production device credentials. Never compile secrets into firmware. */
    /*
     * 首次绑定不带已有凭证；服务端解绑后的重绑使用旧凭证签名设备上报。
     * platform_client_provision() 阻塞等待用户输入验证码，因此本函数只能
     * 在 starter_start_task 中运行，不能放进 app_main 或 SDK 回调。
     */
    platform_provision_result_t result = {0};
    const platform_provision_config_t provision = {
        .mac_address = mac_address,
        .existing_device_id = signed_rebind ? s_tirtc_config.device_id : NULL,
        .existing_device_secret = signed_rebind
                                      ? s_tirtc_config.device_secret
                                      : NULL,
        .discovery_url = DISCOVERY_URL,
        .timeout_seconds = 190,
        .on_verification_code = on_verification_code,
    };
    esp_err_t err = platform_client_provision(&provision, &result);
    starter_media_clear_binding_code();
    if (err != ESP_OK) {
        return err;
    }
    (void)snprintf(s_tirtc_config.device_id,
                   sizeof(s_tirtc_config.device_id),
                   "%s",
                   result.device_id);
    (void)snprintf(s_tirtc_config.device_secret,
                   sizeof(s_tirtc_config.device_secret),
                   "%s",
                   result.device_secret);
    err = runtime_config_save_tirtc(&s_tirtc_config);
    if (err == ESP_OK) {
        ESP_LOGI(TAG, "binding saved for device_id=%s", s_tirtc_config.device_id);
    }
    return err;
}

static void starter_start_task(void *argument)
{
    (void)argument;

    /* 服务发现、HTTP 和 MQTT 都依赖 STA 已拿到 IP。 */
    while (!wifi_manager_connected()) {
        vTaskDelay(pdMS_TO_TICKS(250));
    }

    char mac_address[18];
    char default_client_id[65];
    station_identity(mac_address, default_client_id);

    /* NVS 没有有效凭证时进入验证码绑定，成功后再继续正常启动。 */
    esp_err_t err = runtime_config_load_tirtc(&s_tirtc_config);
    bool credentials_valid = err == ESP_OK &&
                             runtime_config_tirtc_valid(&s_tirtc_config, NULL, 0);
    if (!credentials_valid) {
        memset(&s_tirtc_config, 0, sizeof(s_tirtc_config));
        (void)snprintf(s_tirtc_config.client_id,
                       sizeof(s_tirtc_config.client_id),
                       "%s",
                       default_client_id);
        ESP_LOGW(TAG, "device is not bound; starting verification-code binding");
        err = provision_and_save(mac_address, false);
        if (err != ESP_OK) {
            ESP_LOGE(TAG,
                     "binding did not complete: %s; restart to retry",
                     esp_err_to_name(err));
            vTaskDelete(NULL);
            return;
        }
    }
    if (s_tirtc_config.client_id[0] == '\0') {
        (void)snprintf(s_tirtc_config.client_id,
                       sizeof(s_tirtc_config.client_id),
                       "%s",
                       default_client_id);
    }

    /*
     * 先启动会话状态任务并注册回调，再启动可能产生回调的 TiRTC。
     * 这是必须保持的顺序。
     */
    err = starter_runtime_start(s_tirtc_config.device_id);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "session runtime unavailable: %s", esp_err_to_name(err));
        vTaskDelete(NULL);
        return;
    }

    const platform_client_config_t platform = {
        .device_id = s_tirtc_config.device_id,
        .device_secret = s_tirtc_config.device_secret,
        .client_id = s_tirtc_config.client_id,
        .mac_address = mac_address,
        .discovery_url = DISCOVERY_URL,
    };
    /*
     * 平台信令和 SDK 独立重试。platform_client_start() 返回 NOT_FOUND 表示
     * 本地凭证对应的设备已解绑，只在本次启动中尝试一次签名重绑。
     */
    bool rebind_attempted = false;
    bool tirtc_submitted = false;
    for (;;) {
        while (!wifi_manager_connected()) {
            vTaskDelay(pdMS_TO_TICKS(250));
        }
        if (!platform_client_ready()) {
            esp_err_t platform_err = platform_client_start(&platform);
            if (platform_err == ESP_ERR_NOT_FOUND && !rebind_attempted) {
                rebind_attempted = true;
                ESP_LOGW(TAG, "stored device was unbound; starting signed rebind");
                platform_err = provision_and_save(mac_address, true);
                if (platform_err == ESP_OK) {
                    platform_err = platform_client_start(&platform);
                }
            }
            if (platform_err != ESP_OK) {
                ESP_LOGE(TAG,
                         "platform signaling unavailable: %s",
                         esp_err_to_name(platform_err));
            }
        }

        if (!tirtc_submitted) {
            const char *tirtc_endpoint = platform_client_tirtc_endpoint();
            if (tirtc_endpoint == NULL) {
                ESP_LOGE(TAG, "service discovery did not return tirtc-srv");
                vTaskDelay(pdMS_TO_TICKS(START_RETRY_DELAY_MS));
                continue;
            }
            const starter_tirtc_config_t tirtc = {
                .device_id = s_tirtc_config.device_id,
                .device_secret = s_tirtc_config.device_secret,
                .client_id = s_tirtc_config.client_id,
                .service_endpoint = tirtc_endpoint,
                .max_send_buffer_bytes = 256U * 1024U,
                .log_level = 3,
            };
            int rc = starter_tirtc_start(&tirtc);
            if (rc == 0) {
                tirtc_submitted = true;
            } else {
                ESP_LOGE(TAG, "TiRTC start failed rc=%d", rc);
            }
        }
        if (platform_client_ready() && tirtc_submitted) {
            break;
        }
        ESP_LOGW(TAG, "startup incomplete; retrying in %u ms", START_RETRY_DELAY_MS);
        vTaskDelay(pdMS_TO_TICKS(START_RETRY_DELAY_MS));
    }
    vTaskDelete(NULL);
}

static void init_nvs(void)
{
    /* 仅在 NVS 分区布局升级或空间耗尽时擦除；普通启动保留配网和绑定数据。 */
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        err = nvs_flash_init();
    }
    ESP_ERROR_CHECK(err);
}

void app_main(void)
{
    /* app_main 保持短小且不等待网络，耗时启动工作交给 starter_start_task。 */
    init_nvs();
    esp_chip_info_t chip;
    esp_chip_info(&chip);
    ESP_LOGI(TAG,
             "ESP-IDF %s, cores=%u, revision=%u",
             esp_get_idf_version(),
             chip.cores,
             chip.revision);
    ESP_LOGI(TAG,
             "heap internal=%" PRIu32 " bytes, PSRAM=%" PRIu32 " bytes",
             heap_caps_get_total_size(MALLOC_CAP_INTERNAL),
             heap_caps_get_total_size(MALLOC_CAP_SPIRAM));
    ESP_LOGI(TAG, "TiRTC version: %s", starter_tirtc_version());
    ESP_LOGI(TAG, "TiRTC build: %s", starter_tirtc_build_info());

    ESP_ERROR_CHECK(starter_media_init());
    ESP_LOGI(TAG,
             "post-media heap internal free=%" PRIu32 " largest=%" PRIu32
             " PSRAM free=%" PRIu32,
             heap_caps_get_free_size(MALLOC_CAP_INTERNAL),
             heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL),
             heap_caps_get_free_size(MALLOC_CAP_SPIRAM));
    s_control_queue = xQueueCreate(4, sizeof(starter_media_control_intent_t));
    if (s_control_queue == NULL ||
        xTaskCreate(control_task, "product_controls", 3072, NULL, 4, NULL) != pdPASS) {
        ESP_LOGE(TAG, "cannot create product controls task");
    } else {
        starter_media_set_control_handler(on_control_intent, NULL);
    }
    ESP_ERROR_CHECK(wifi_manager_start());
    ESP_ERROR_CHECK(starter_console_start());
    if (xTaskCreate(starter_start_task,
                    "starter_start",
                    24576,
                    NULL,
                    4,
                    NULL) != pdPASS) {
        ESP_LOGE(TAG, "cannot create startup task");
    }
}
