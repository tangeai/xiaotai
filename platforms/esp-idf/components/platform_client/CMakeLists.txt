set(xiaotai_platform_client_dir "${CMAKE_CURRENT_LIST_DIR}")

idf_component_register(
    SRCS "${xiaotai_platform_client_dir}/src/platform_client.c"
    INCLUDE_DIRS "${xiaotai_platform_client_dir}/include"
    REQUIRES esp_http_client esp_netif mqtt mbedtls json
    PRIV_REQUIRES esp-tls esp_timer
)
