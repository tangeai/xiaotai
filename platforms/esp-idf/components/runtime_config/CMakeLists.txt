set(xiaotai_runtime_config_dir "${CMAKE_CURRENT_LIST_DIR}")

idf_component_register(
    SRCS "${xiaotai_runtime_config_dir}/src/runtime_config.c"
         "${xiaotai_runtime_config_dir}/src/config_store.c"
    INCLUDE_DIRS "${xiaotai_runtime_config_dir}/include"
    PRIV_INCLUDE_DIRS "${xiaotai_runtime_config_dir}/../../../common/include"
    REQUIRES nvs_flash
    PRIV_REQUIRES esp_hw_support freertos
)
