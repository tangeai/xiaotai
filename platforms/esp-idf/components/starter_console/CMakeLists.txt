set(xiaotai_starter_console_dir "${CMAKE_CURRENT_LIST_DIR}")

idf_component_register(
    SRCS "${xiaotai_starter_console_dir}/src/starter_console.c"
    INCLUDE_DIRS "${xiaotai_starter_console_dir}/include"
    REQUIRES console
    PRIV_REQUIRES
        esp_app_format
        esp_system
        esp_timer
        freertos
        platform_client
        runtime_config
        starter_media
        starter_product
        starter_runtime
        starter_tirtc
        starter_voice
        wifi_manager
)
