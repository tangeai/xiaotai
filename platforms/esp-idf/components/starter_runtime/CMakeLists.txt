set(xiaotai_starter_runtime_dir "${CMAKE_CURRENT_LIST_DIR}")

idf_component_register(
    SRCS
        "${xiaotai_starter_runtime_dir}/src/starter_runtime.c"
        "${xiaotai_starter_runtime_dir}/../../../../product/src/xiaotai_runtime.c"
    INCLUDE_DIRS
        "${xiaotai_starter_runtime_dir}/include"
        "${xiaotai_starter_runtime_dir}/../../../../product/include"
    REQUIRES json
    PRIV_REQUIRES
        esp_timer
        esp_system
        freertos
        platform_client
        runtime_config
        starter_media
        starter_tirtc
)
