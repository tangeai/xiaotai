idf_component_register(
    SRCS "app_main.c"
    INCLUDE_DIRS "."
    PRIV_REQUIRES esp_system nvs_flash platform_client runtime_config
                  starter_console starter_media starter_runtime starter_tirtc
                  wifi_manager
)
