/* Read-only development evidence. Configured pins do not identify a carrier. */
#include <stdio.h>
#include <stdint.h>
#include "sdkconfig.h"
#include "esp_chip_info.h"
#include "esp_flash.h"
#include "esp_heap_caps.h"
#include "esp_system.h"
#include "esp_wifi.h"
#include "esp_console.h"
#include "esp_hosted.h"
#include "starter_tirtc.h"
#include "wifi_manager.h"

static int command_board_probe(int argc, char **argv)
{
    (void)argv;
    if (argc != 1) { printf("usage: board-probe\n"); return 1; }
    if (starter_tirtc_connected()) {
        printf("BOARD-PROBE: run while idle, before starting a call\n");
        return 1;
    }
    esp_chip_info_t chip;
    esp_chip_info(&chip);
    uint32_t flash_bytes = 0;
    int flash_rc = esp_flash_get_size(NULL, &flash_bytes);
    printf("BOARD-PROBE: chip-model=%d revision=%u.%u cores=%u idf=%s\n",
           chip.model, chip.revision / 100U, chip.revision % 100U,
           chip.cores, esp_get_idf_version());
    printf("BOARD-PROBE: flash-rc=%d flash-bytes=%lu psram-allocator-bytes=%lu cpu-config-mhz=%u\n",
           flash_rc, (unsigned long)flash_bytes,
           (unsigned long)heap_caps_get_total_size(MALLOC_CAP_SPIRAM),
           CONFIG_ESP_DEFAULT_CPU_FREQ_MHZ);
    printf("BOARD-PROBE: carrier=unverified; configured SDIO width=%u clock-khz=%u clk/cmd/d0/d1/d2/d3/reset=%d/%d/%d/%d/%d/%d/%d\n",
           CONFIG_ESP_HOSTED_SDIO_BUS_WIDTH, CONFIG_ESP_HOSTED_SDIO_CLOCK_FREQ_KHZ,
           CONFIG_ESP_HOSTED_SDIO_PIN_CLK, CONFIG_ESP_HOSTED_SDIO_PIN_CMD,
           CONFIG_ESP_HOSTED_SDIO_PIN_D0, CONFIG_ESP_HOSTED_SDIO_PIN_D1,
           CONFIG_ESP_HOSTED_SDIO_PIN_D2, CONFIG_ESP_HOSTED_SDIO_PIN_D3,
           CONFIG_ESP_HOSTED_SDIO_GPIO_RESET_SLAVE);
    printf("BOARD-PROBE: host-source=Hosted-1.4.7+local-patches; C6 app identity unavailable in this host API\n");
    if (!wifi_manager_connected()) {
        printf("BOARD-PROBE: C6/Wi-Fi query skipped: network not ready\n");
        return 0;
    }
    esp_hosted_coprocessor_fwver_t cp = {0};
    int rc = esp_hosted_get_coprocessor_fwversion(&cp);
    if (rc != ESP_OK || (cp.major1 == 0 && cp.minor1 == 0 && cp.patch1 == 0)) {
        printf("BOARD-PROBE: C6 Hosted version=unknown rc=%d; no update attempted\n", rc);
    } else {
        printf("BOARD-PROBE: C6 Hosted version=%lu.%lu.%lu; not a full app identity\n",
               (unsigned long)cp.major1, (unsigned long)cp.minor1,
               (unsigned long)cp.patch1);
    }
    wifi_ap_record_t ap = {0};
    rc = esp_wifi_sta_get_ap_info(&ap);
    if (rc == ESP_OK) printf("BOARD-PROBE: wifi-rssi=%d dBm channel=%u\n", ap.rssi, ap.primary);
    else printf("BOARD-PROBE: Wi-Fi AP query unavailable rc=%d\n", rc);
    uint8_t protocols = 0;
    rc = esp_wifi_get_protocol(WIFI_IF_STA, &protocols);
    if (rc == ESP_OK) printf("BOARD-PROBE: wifi-enabled-protocol-mask=0x%x (capability setting, not negotiated PHY)\n", protocols);
    else printf("BOARD-PROBE: Wi-Fi protocol query unavailable rc=%d\n", rc);
    return 0;
}

int p4_board_probe_register(void)
{
    const esp_console_cmd_t command = {
        .command = "board-probe", .help = "Read-only P4/C6 evidence; run idle after Wi-Fi connects",
        .hint = NULL, .func = command_board_probe,
    };
    return esp_console_cmd_register(&command);
}
