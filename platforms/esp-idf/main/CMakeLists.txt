set(xiaotai_main_dir "${CMAKE_CURRENT_LIST_DIR}")

idf_component_register(
    SRCS "${xiaotai_main_dir}/app_main.c"
    INCLUDE_DIRS "${xiaotai_main_dir}"
    PRIV_REQUIRES
        esp_app_format
        esp_system
        esp_wifi
        nvs_flash
        platform_client
        runtime_config
        starter_button
        starter_console
        starter_media
        starter_product
        starter_runtime
        starter_tirtc
        starter_voice
        wifi_manager
        ${XIAOTAI_MAIN_EXTRA_REQUIRES}
)
