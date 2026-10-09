/*
 * 工程组合根：只负责初始化基础设施、组装模块和持有启动任务。
 *
 * 启动顺序：NVS -> V1.0.1 媒体适配器 -> 产品 UI -> Wi-Fi -> 后台上线任务。
 * 后台任务等待网络、完成必要的首次绑定，优先为 TiRTC 保留连续内部堆；
 * TiRTC bootstrap 后异步初始化专用唤醒，再运行平台 MQTT/TLS 和开发控制任务。
 * 当前启动编排并非离线唤醒产品。H5/AI/语音呼叫顺序由 starter_runtime
 * 隐藏，app_main 不直接处理 SDK 回调或音视频帧。
 */
#include <inttypes.h>
#include <stdbool.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "esp_app_desc.h"
#include "esp_chip_info.h"
#include "esp_heap_caps.h"
#include "esp_idf_version.h"
#include "esp_log.h"
#include "esp_mac.h"
#include "esp_wifi.h"
#include "esp_rom_sys.h"
#include "freertos/FreeRTOS.h"
#include "freertos/idf_additions.h"
#include "freertos/task.h"
#include "nvs_flash.h"
#include "platform_client.h"
#include "runtime_config.h"
#include "starter_button.h"
#include "starter_console.h"
#include "starter_media.h"
#include "starter_product.h"
#include "starter_runtime.h"
#include "starter_tirtc.h"
#include "starter_voice.h"
#include "sdkconfig.h"
#include "wifi_manager.h"

/* 联调阶段先使用已验证的 HTTP 平台服务发现；HTTPS 留到功能验收后启用。 */
#define DISCOVERY_URL CONFIG_XIAOTAI_DISCOVERY_URL
#define START_RETRY_DELAY_MS 5000U
#define STARTER_TASK_STACK_BYTES 13312U
#define PLATFORM_REQUEST_TASK_STACK_BYTES 13312U
#define VERIFICATION_PROMPT_REPEAT_COUNT 3U
#define VERIFICATION_PROMPT_GAP_MS 350U
#define TIRTC_INTERNAL_RESERVE_BYTES (20U * 1024U)

/* Existing constrained boards keep the reference-size transport queue unless
 * their project Kconfig opts into a larger board media contract. */
#ifndef CONFIG_XIAOTAI_TIRTC_MAX_SEND_BUFFER_BYTES
#define CONFIG_XIAOTAI_TIRTC_MAX_SEND_BUFFER_BYTES (256U * 1024U)
#endif

static const char *TAG = "starter_main";
static runtime_tirtc_config_t s_tirtc_config;
static void *s_tirtc_internal_reserve;

static void on_local_voice_result(const starter_voice_result_t *result, void *user_data)
{
    (void)user_data;
    if (result == NULL || starter_runtime_status().state != STARTER_RUNTIME_WAITING) return;
    starter_voice_result_t wake = *result;
    wake.wake_token = starter_media_prepare_ai_preroll(wake.captured_ms);
    if (wake.wake_token == 0) return;
    if (starter_product_submit_voice(&wake) != ESP_OK)
        starter_media_cancel_ai_preroll(wake.wake_token);
}

static void allocation_failed(size_t size, uint32_t caps, const char *function_name)
{
    /* ROM printf avoids allocating again while reporting the failed allocation. */
    esp_rom_printf("E heap_alloc: failed size=%u caps=0x%08x function=%s\n",
                   (unsigned)size,
                   (unsigned)caps,
                   function_name == NULL ? "unknown" : function_name);
}

static void log_heap_snapshot(const char *stage)
{
    ESP_LOGI(TAG,
             "heap %s internal=%u min=%u largest=%u dma=%u dma-largest=%u "
             "psram=%u psram-largest=%u",
             stage,
             (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL |
                                                MALLOC_CAP_8BIT),
             (unsigned)heap_caps_get_minimum_free_size(MALLOC_CAP_INTERNAL |
                                                        MALLOC_CAP_8BIT),
             (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL |
                                                        MALLOC_CAP_8BIT),
             (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL |
                                                MALLOC_CAP_DMA),
             (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL |
                                                        MALLOC_CAP_DMA),
             (unsigned)heap_caps_get_free_size(MALLOC_CAP_SPIRAM |
                                                MALLOC_CAP_8BIT),
             (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_SPIRAM |
                                                        MALLOC_CAP_8BIT));
}

/*
 * 启动任务必须使用内部栈，因为它会读 NVS；平台已上线后，长期 HTTP 循环
 * 不再触碰 NVS/模型分区，迁到 PSRAM 栈，归还 TiRTC 外连需要的连续内部堆。
 */
static void platform_request_task(void *argument)
{
    (void)argument;
    for (;;) {
        esp_err_t err = platform_client_run_request_loop();
        ESP_LOGE(TAG,
                 "platform request loop unavailable: %s; retrying in %u ms",
                 esp_err_to_name(err),
                 START_RETRY_DELAY_MS);
        vTaskDelay(pdMS_TO_TICKS(START_RETRY_DELAY_MS));
    }
}

/* 使用 STA MAC 生成稳定的设备指纹和默认 TiRTC client_id。 */
static esp_err_t station_identity(char mac_address[18], char client_id[65])
{
    uint8_t mac[6];
#if CONFIG_IDF_TARGET_ESP32P4
    /* P4 has no native Wi-Fi MAC. After got-IP, query the active C6 station
     * through esp_wifi_remote; do not invent a P4 eFuse Wi-Fi identity. */
    esp_err_t err = esp_wifi_get_mac(WIFI_IF_STA, mac);
#else
    esp_err_t err = esp_read_mac(mac, ESP_MAC_WIFI_STA);
#endif
    if (err != ESP_OK) {
        return err;
    }
    (void)snprintf(mac_address,
                   18,
                   "%02X:%02X:%02X:%02X:%02X:%02X",
                   mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
    (void)snprintf(client_id,
                   65,
                   CONFIG_IDF_TARGET "-%02x%02x%02x%02x%02x%02x",
                   mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
    return ESP_OK;
}

static atomic_bool s_verification_prompt_cancelled;

static void cancel_verification_prompt(void *user_data)
{
    (void)user_data;
    atomic_store(&s_verification_prompt_cancelled, true);
    starter_media_cancel_pcm8k_playback();
}

static esp_err_t play_verification_prompt(const int16_t *pcm,
                                          size_t sample_count,
                                          void *user_data)
{
    (void)user_data;
    uint32_t epoch = starter_media_playback_epoch();
    for (unsigned repeat = 0; repeat < VERIFICATION_PROMPT_REPEAT_COUNT; ++repeat) {
        if (atomic_load(&s_verification_prompt_cancelled)) return ESP_OK;
        esp_err_t err = starter_media_play_pcm8k_at_epoch(pcm, sample_count, epoch);
        if (atomic_load(&s_verification_prompt_cancelled)) return ESP_OK;
        if (err != ESP_OK) return err;
        if (repeat + 1U < VERIFICATION_PROMPT_REPEAT_COUNT) {
            for (unsigned waited = 0; waited < VERIFICATION_PROMPT_GAP_MS; waited += 20U) {
                if (atomic_load(&s_verification_prompt_cancelled)) return ESP_OK;
                vTaskDelay(pdMS_TO_TICKS(20));
            }
        }
    }
    ESP_LOGI(TAG, "verification prompt completed repeats=%u",
             VERIFICATION_PROMPT_REPEAT_COUNT);
    return ESP_OK;
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
    atomic_store(&s_verification_prompt_cancelled, false);
    platform_provision_result_t result = {0};
    const platform_provision_config_t provision = {
        .mac_address = mac_address,
        .existing_device_id = signed_rebind ? s_tirtc_config.device_id : NULL,
        .existing_device_secret = signed_rebind
                                      ? s_tirtc_config.device_secret
                                      : NULL,
        .discovery_url = DISCOVERY_URL,
        .timeout_seconds = 190,
        .prompt_callback = play_verification_prompt,
        .prompt_cancel_callback = cancel_verification_prompt,
        .prompt_user_data = NULL,
    };
    esp_err_t err = platform_client_provision(&provision, &result);
    if (err != ESP_OK) {
        return err;
    }
    /* Keep the active identity unchanged until the new credentials are durable.
     * A failed save must retry signed provisioning with the original identity. */
    runtime_tirtc_config_t candidate = s_tirtc_config;
    (void)snprintf(candidate.device_id, sizeof(candidate.device_id), "%s", result.device_id);
    (void)snprintf(candidate.device_secret, sizeof(candidate.device_secret), "%s", result.device_secret);
    err = runtime_config_save_tirtc(&candidate);
    if (err == ESP_OK) {
        s_tirtc_config = candidate;
        ESP_LOGI(TAG, "binding saved for device_id=%s", s_tirtc_config.device_id);
        starter_product_binding_saved();
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
    while (station_identity(mac_address, default_client_id) != ESP_OK) {
        ESP_LOGW(TAG, "station identity unavailable; retrying");
        vTaskDelay(pdMS_TO_TICKS(START_RETRY_DELAY_MS));
    }

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
        for (;;) {
            while (!wifi_manager_connected()) {
                vTaskDelay(pdMS_TO_TICKS(250));
            }
            err = provision_and_save(mac_address, false);
            if (err == ESP_OK) {
                break;
            }
            ESP_LOGE(TAG,
                     "binding incomplete; retrying in %u ms: %s",
                     START_RETRY_DELAY_MS,
                     esp_err_to_name(err));
            vTaskDelay(pdMS_TO_TICKS(START_RETRY_DELAY_MS));
        }
    } else {
        ESP_LOGI(TAG,
                 "stored device binding found; skipping verification-code binding");
    }
    if (s_tirtc_config.client_id[0] == '\0') {
        (void)snprintf(s_tirtc_config.client_id,
                       sizeof(s_tirtc_config.client_id),
                       "%s",
                       default_client_id);
    }
    ESP_LOGI(TAG,
             "starter task internal stack remaining=%u bytes",
             (unsigned)uxTaskGetStackHighWaterMark(NULL));

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
     * 本地凭证对应的设备已解绑，保持在验证码页并持续尝试签名重绑。
     */
    bool tirtc_submitted = false;
    bool controls_started = false;
    bool wake_submitted = false;
    for (;;) {
        while (!wifi_manager_connected()) {
            vTaskDelay(pdMS_TO_TICKS(250));
        }

        /*
         * TIRTC_FIRST_CONTIGUOUS_HEAP: TiRtcStart needs one 16+ KiB internal
         * block. Reserve it before MQTT/TLS and development-control task stacks
         * fragment the remaining internal heap.
         */
        if (!tirtc_submitted) {
            log_heap_snapshot("pre-tirtc-bootstrap");
            if (s_tirtc_internal_reserve != NULL) {
                heap_caps_free(s_tirtc_internal_reserve);
                s_tirtc_internal_reserve = NULL;
                ESP_LOGI(TAG,
                         "released %u-byte internal TiRTC bootstrap reserve",
                         TIRTC_INTERNAL_RESERVE_BYTES);
            }
            const starter_tirtc_config_t tirtc = {
                .device_id = s_tirtc_config.device_id,
                .device_secret = s_tirtc_config.device_secret,
                .client_id = s_tirtc_config.client_id,
                /* 由板级媒体合同配置：P4 的 H264 访问单元可达 256 KiB，
                 * 全双工音视频需要更大余量，避免一个峰值帧堵住音频。 */
                .max_send_buffer_bytes = CONFIG_XIAOTAI_TIRTC_MAX_SEND_BUFFER_BYTES,
                .log_level = 3,
            };
            int rc = starter_tirtc_start(&tirtc);
            if (rc == 0) {
                tirtc_submitted = true;
                ESP_LOGI(TAG,
                         "TiRTC send buffer configured=%u bytes",
                         (unsigned)tirtc.max_send_buffer_bytes);
                log_heap_snapshot("post-tirtc-submit");
            } else {
                ESP_LOGE(TAG, "TiRTC start failed rc=%d", rc);
            }
        }
        if (!tirtc_submitted) {
            ESP_LOGW(TAG,
                     "startup incomplete; retrying in %u ms",
                     START_RETRY_DELAY_MS);
            vTaskDelay(pdMS_TO_TICKS(START_RETRY_DELAY_MS));
            continue;
        }
        if (!starter_tirtc_started()) {
            /* TiRtcStart is asynchronous; avoid overlapping MQTT/TLS startup. */
            vTaskDelay(pdMS_TO_TICKS(100));
            continue;
        }

        /*
         * TiRtcWhipConnect later needs a 16,717-byte contiguous internal block.
         * Claim a 17 KiB block before voice/MQTT can fragment it; the runtime
         * gives it back only after MQTT has been destroyed for an external call.
         */
        if (starter_runtime_arm_external_connect_reserve() != ESP_OK) {
            ESP_LOGW(TAG, "external-connect reserve unavailable; retrying startup");
            vTaskDelay(pdMS_TO_TICKS(START_RETRY_DELAY_MS));
            continue;
        }

        if (!wake_submitted) {
            wake_submitted = true;
            esp_err_t wake_err = starter_voice_start(on_local_voice_result, NULL);
            if (wake_err != ESP_OK)
                ESP_LOGE(TAG, "wake task unavailable: %s", esp_err_to_name(wake_err));
        }

        if (!platform_client_ready()) {
            esp_err_t platform_err = platform_client_start(&platform);
            if (platform_err == ESP_ERR_NOT_FOUND) {
                ESP_LOGW(TAG, "stored device was unbound; starting signed rebind");
                platform_err = provision_and_save(mac_address, true);
                if (platform_err == ESP_OK) {
                    ESP_LOGI(TAG, "signed rebind saved; restarting with new credentials");
                    vTaskDelay(pdMS_TO_TICKS(1000));
                    esp_restart();
                }
            }
            if (platform_err != ESP_OK) {
                ESP_LOGE(TAG,
                         "platform signaling unavailable: %s",
                         esp_err_to_name(platform_err));
            }
        }
        if (platform_client_ready() && tirtc_submitted) {
            if (!controls_started) {
                esp_err_t console_err = starter_console_start();
                esp_err_t button_err = starter_button_start();
                controls_started = console_err == ESP_OK && button_err == ESP_OK;
                if (!controls_started) {
                    ESP_LOGW(TAG,
                             "development controls deferred: console=%s button=%s",
                             esp_err_to_name(console_err),
                             esp_err_to_name(button_err));
                }
            }
            log_heap_snapshot("pre-platform-worker");
            if (xTaskCreateWithCaps(platform_request_task,
                                    "platform_http",
                                    PLATFORM_REQUEST_TASK_STACK_BYTES,
                                    NULL,
                                    4,
                                    NULL,
                                    MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT) ==
                pdPASS) {
                ESP_LOGI(TAG,
                         "platform request loop moved to PSRAM; releasing "
                         "cache-safe startup stack");
                break;
            }
            ESP_LOGE(TAG, "cannot create PSRAM platform request task");
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
    ESP_ERROR_CHECK(heap_caps_register_failed_alloc_callback(allocation_failed));
    init_nvs();
    if (setenv("TZ", "CST-8", 1) != 0) {
        ESP_LOGW(TAG, "cannot configure Asia/Shanghai timezone");
    }
    tzset();
    const esp_app_desc_t *app = esp_app_get_description();
    char elf_sha256[65];
    (void)esp_app_get_elf_sha256(elf_sha256, sizeof(elf_sha256));
    ESP_LOGI(TAG,
             "firmware version=%s built=%s %s elf-sha256=%s",
             app->version,
             app->date,
             app->time,
             elf_sha256);
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
    log_heap_snapshot("post-media");
    ESP_ERROR_CHECK(starter_product_start());
    log_heap_snapshot("post-product-ui");
    /* 本地语音模型在 TiRTC TLS 建立后加载，避免两个启动峰值竞争内部 SRAM。 */
    ESP_ERROR_CHECK(wifi_manager_start());
    log_heap_snapshot("post-wifi-start");
    /* NTP_SYNC_BACKGROUND_TASK: SNTP/HMAC waits never block app_main or LVGL. */
    /* This worker reads/writes NVS and loads the model partition. Both paths
     * temporarily disable the flash cache, so ESP-IDF requires an internal
     * stack even when external task stacks are otherwise enabled. */
    if (xTaskCreate(starter_start_task,
                    "starter_start",
                    STARTER_TASK_STACK_BYTES,
                    NULL,
                    4,
                    NULL) != pdPASS) {
        ESP_LOGE(TAG, "cannot create startup task");
    }
    /*
     * Wi-Fi and starter_start own long-lived internal stacks. Reserve the
     * TiRTC bootstrap block only after both allocations have succeeded; the
     * startup task is still waiting for a station IP at this point.
     */
    s_tirtc_internal_reserve = heap_caps_malloc(
        TIRTC_INTERNAL_RESERVE_BYTES,
        MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    if (s_tirtc_internal_reserve == NULL) {
        ESP_LOGW(TAG, "could not reserve contiguous internal heap for TiRTC");
    } else {
        ESP_LOGI(TAG,
                 "reserved %u internal bytes for later TiRTC bootstrap",
                 TIRTC_INTERNAL_RESERVE_BYTES);
    }
}
