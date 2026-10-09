set(xiaotai_runtime_config_dir "${CMAKE_CURRENT_LIST_DIR}")

idf_component_register(
    SRCS "${xiaotai_runtime_config_dir}/src/runtime_config.c"
    INCLUDE_DIRS "${xiaotai_runtime_config_dir}/include"
    REQUIRES nvs_flash
    PRIV_REQUIRES esp_hw_support freertos
)
