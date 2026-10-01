set(xiaotai_wifi_manager_dir "${CMAKE_CURRENT_LIST_DIR}")

idf_component_register(
    SRCS
        "${xiaotai_wifi_manager_dir}/src/wifi_manager.c"
        "${xiaotai_wifi_manager_dir}/src/captive_dns.c"
    INCLUDE_DIRS "${xiaotai_wifi_manager_dir}/include"
    PRIV_INCLUDE_DIRS "${xiaotai_wifi_manager_dir}/src"
    REQUIRES esp_wifi esp_netif esp_event nvs_flash
    PRIV_REQUIRES esp_http_server json esp_system lwip
)
