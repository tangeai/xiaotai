idf_component_register(
    SRCS "src/wifi_manager.c" "src/wifi_captive_dns.c"
    INCLUDE_DIRS "include"
    REQUIRES esp_wifi esp_netif esp_event nvs_flash
    PRIV_REQUIRES esp_http_server json esp_system lwip
)
