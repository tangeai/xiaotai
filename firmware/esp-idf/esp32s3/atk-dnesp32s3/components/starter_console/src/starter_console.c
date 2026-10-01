/*
 * 开发期串口控制台。
 *
 * 这个模块是人工操作的 adapter：只解析参数、调用公开接口并打印结果。
 * 它不拥有 Wi-Fi、凭证或会话状态。量产按键/UI 应调用相同接口，而不是复用
 * ESP console 的命令处理函数。
 */
#include "starter_console.h"

#include <stdio.h>
#include <string.h>

#include "esp_check.h"
#include "esp_console.h"
#include "esp_heap_caps.h"
#include "esp_system.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "platform_client.h"
#include "runtime_config.h"
#include "starter_media.h"
#include "starter_runtime.h"
#include "starter_tirtc.h"
#include "wifi_manager.h"

static esp_console_repl_t *s_repl;

/* 汇总各深模块的只读快照，不读取或打印 device_secret。 */
static int command_status(int argc, char **argv)
{
    (void)argc;
    (void)argv;
    starter_runtime_status_t runtime = starter_runtime_status();
    starter_media_status_t media = starter_media_status();
    printf("Wi-Fi: %s", wifi_manager_connected() ? "connected" : "disconnected");
    if (wifi_manager_provisioning()) {
        printf(" (SoftAP %s)", wifi_manager_provisioning_ssid());
    }
    printf("\nPlatform: api=%s mqtt=%s\n",
           platform_client_ready() ? "ready" : "offline",
           platform_client_mqtt_connected() ? "connected" : "disconnected");
    if (platform_client_provisioning()) {
        printf("Binding: verification code=%s\n",
               platform_client_verification_code());
    }
    printf("TiRTC: started=%s connection=%s mode=%d send-buffer=%u\n",
           starter_tirtc_started() ? "yes" : "no",
           starter_tirtc_connected() ? "active" : "none",
           (int)starter_tirtc_mode(),
           (unsigned)starter_tirtc_send_buffer_used());
    printf("Session: %s generation=%lu connection=%lu last-error=%d\n",
           starter_runtime_state_name(runtime.state),
           (unsigned long)runtime.session_generation,
           (unsigned long)runtime.connection_generation,
           runtime.last_error);
    printf("Media: active=%s audio-tx=%lu video-tx=%lu audio-rx=%lu dropped=%lu\n",
           media.active ? "yes" : "no",
           (unsigned long)media.audio_sent,
           (unsigned long)media.video_sent,
           (unsigned long)media.audio_received,
           (unsigned long)media.audio_dropped);
    printf("Heap: internal-free=%u largest=%u psram-free=%u\n",
           (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL),
           (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL),
           (unsigned)heap_caps_get_free_size(MALLOC_CAP_SPIRAM));
    return 0;
}

static int command_ai_start(int argc, char **argv)
{
    /*
     * TODO(product-control): 在板级按键任务完成消抖后，按下时调用
     * starter_runtime_ai_start()，松开时调用 starter_runtime_ai_stop()。
     * GPIO 和按键消抖细节不要进入 starter_runtime。
     */
    (void)argv;
    if (argc != 1) {
        printf("usage: ai-start\n");
        return 1;
    }
    esp_err_t err = starter_runtime_ai_start();
    if (err != ESP_OK) {
        printf("AI start rejected: %s\n", esp_err_to_name(err));
        return 1;
    }
    return 0;
}

static int command_ai_stop(int argc, char **argv)
{
    (void)argv;
    if (argc != 1) {
        printf("usage: ai-stop\n");
        return 1;
    }
    esp_err_t err = starter_runtime_ai_stop();
    if (err != ESP_OK) {
        printf("AI stop rejected: %s\n", esp_err_to_name(err));
        return 1;
    }
    return 0;
}

static int command_wifi_set(int argc, char **argv)
{
    /* 保存后重启，让 wifi_manager 从单一的正常启动路径应用新配置。 */
    if (argc != 3) {
        printf("usage: wifi-set <ssid> <password>; use \"\" for an open network\n");
        return 1;
    }
    char error[80];
    if (!wifi_manager_credentials_valid(argv[1], argv[2], error, sizeof(error))) {
        printf("invalid Wi-Fi config: %s\n", error);
        return 1;
    }
    esp_err_t err = wifi_manager_save_credentials(argv[1], argv[2]);
    if (err != ESP_OK) {
        printf("save failed: %s\n", esp_err_to_name(err));
        return 1;
    }
    printf("Wi-Fi config saved; restarting...\n");
    vTaskDelay(pdMS_TO_TICKS(300));
    esp_restart();
    return 0;
}

static int command_wifi_clear(int argc, char **argv)
{
    (void)argc;
    (void)argv;
    esp_err_t err = wifi_manager_forget_credentials();
    if (err != ESP_OK) {
        printf("clear failed: %s\n", esp_err_to_name(err));
        return 1;
    }
    printf("Wi-Fi config cleared; restarting into SoftAP mode...\n");
    vTaskDelay(pdMS_TO_TICKS(300));
    esp_restart();
    return 0;
}

static int command_tirtc_set(int argc, char **argv)
{
    /* 仅用于受控联调；正常设备通过验证码绑定获得并保存凭证。 */
    if (argc < 3 || argc > 4) {
        printf("usage: tirtc-set <device_id> <device_secret> [client_id]\n");
        return 1;
    }
    runtime_tirtc_config_t config = {0};
    if (strlen(argv[1]) >= sizeof(config.device_id) ||
        strlen(argv[2]) >= sizeof(config.device_secret) ||
        (argc == 4 && strlen(argv[3]) >= sizeof(config.client_id))) {
        printf("one or more arguments are too long\n");
        return 1;
    }
    (void)snprintf(config.device_id, sizeof(config.device_id), "%s", argv[1]);
    (void)snprintf(config.device_secret, sizeof(config.device_secret), "%s", argv[2]);
    if (argc == 4) {
        (void)snprintf(config.client_id, sizeof(config.client_id), "%s", argv[3]);
    }
    esp_err_t err = runtime_config_save_tirtc(&config);
    if (err != ESP_OK) {
        printf("save failed: %s\n", esp_err_to_name(err));
        return 1;
    }
    printf("device credentials saved (secret is not displayed); restarting...\n");
    vTaskDelay(pdMS_TO_TICKS(300));
    esp_restart();
    return 0;
}

static int command_tirtc_clear(int argc, char **argv)
{
    (void)argc;
    (void)argv;
    esp_err_t err = runtime_config_clear_tirtc();
    if (err != ESP_OK) {
        printf("clear failed: %s\n", esp_err_to_name(err));
        return 1;
    }
    printf("device credentials cleared; restarting into binding flow...\n");
    vTaskDelay(pdMS_TO_TICKS(300));
    esp_restart();
    return 0;
}

static int command_restart(int argc, char **argv)
{
    (void)argc;
    (void)argv;
    printf("restarting...\n");
    vTaskDelay(pdMS_TO_TICKS(200));
    esp_restart();
    return 0;
}

static esp_err_t register_command(const char *name,
                                  const char *help,
                                  esp_console_cmd_func_t function)
{
    const esp_console_cmd_t command = {
        .command = name,
        .help = help,
        .func = function,
    };
    return esp_console_cmd_register(&command);
}

esp_err_t starter_console_start(void)
{
    if (s_repl != NULL) {
        return ESP_OK;
    }
    ESP_RETURN_ON_ERROR(esp_console_register_help_command(),
                        "starter_console",
                        "register help");
    ESP_RETURN_ON_ERROR(register_command("status", "Show starter status", command_status),
                        "starter_console", "register status");
    ESP_RETURN_ON_ERROR(register_command("ai-start", "Start AI talk", command_ai_start),
                        "starter_console", "register ai-start");
    ESP_RETURN_ON_ERROR(register_command("ai-stop", "Stop AI talk", command_ai_stop),
                        "starter_console", "register ai-stop");
    ESP_RETURN_ON_ERROR(register_command("wifi-set", "wifi-set <ssid> <password>", command_wifi_set),
                        "starter_console", "register wifi-set");
    ESP_RETURN_ON_ERROR(register_command("wifi-clear", "Clear Wi-Fi and use SoftAP", command_wifi_clear),
                        "starter_console", "register wifi-clear");
    ESP_RETURN_ON_ERROR(register_command("tirtc-set", "Pre-load device credentials", command_tirtc_set),
                        "starter_console", "register tirtc-set");
    ESP_RETURN_ON_ERROR(register_command("tirtc-clear", "Clear device credentials", command_tirtc_clear),
                        "starter_console", "register tirtc-clear");
    ESP_RETURN_ON_ERROR(register_command("restart", "Restart the device", command_restart),
                        "starter_console", "register restart");

    /* sdkconfig.defaults 默认走 USB Serial/JTAG，UART 分支便于其他板型复用。 */
    esp_console_repl_config_t repl = ESP_CONSOLE_REPL_CONFIG_DEFAULT();
    repl.prompt = "starter> ";
    repl.max_cmdline_length = 512;
#if defined(CONFIG_ESP_CONSOLE_USB_SERIAL_JTAG)
    esp_console_dev_usb_serial_jtag_config_t device =
        ESP_CONSOLE_DEV_USB_SERIAL_JTAG_CONFIG_DEFAULT();
    ESP_RETURN_ON_ERROR(esp_console_new_repl_usb_serial_jtag(&device, &repl, &s_repl),
                        "starter_console", "create USB Serial/JTAG REPL");
#elif defined(CONFIG_ESP_CONSOLE_UART_DEFAULT) || defined(CONFIG_ESP_CONSOLE_UART_CUSTOM)
    esp_console_dev_uart_config_t device = ESP_CONSOLE_DEV_UART_CONFIG_DEFAULT();
    ESP_RETURN_ON_ERROR(esp_console_new_repl_uart(&device, &repl, &s_repl),
                        "starter_console", "create UART REPL");
#else
#error Unsupported console transport
#endif
    return esp_console_start_repl(s_repl);
}
