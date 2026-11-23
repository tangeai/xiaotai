set(xiaotai_starter_runtime_dir "${CMAKE_CURRENT_LIST_DIR}")

idf_component_register(
    SRCS
        "${xiaotai_starter_runtime_dir}/src/starter_recognition.c"
        "${xiaotai_starter_runtime_dir}/../../../../product/src/xiaotai_recognition.c"
        "${xiaotai_starter_runtime_dir}/src/starter_runtime.c"
        "${xiaotai_starter_runtime_dir}/../../../../product/src/xiaotai_runtime.c"
        "${xiaotai_starter_runtime_dir}/../../../../product/src/xiaotai_ai_protocol.c"
        "${xiaotai_starter_runtime_dir}/../../../../product/src/xiaotai_ai_view.c"
    INCLUDE_DIRS
        "${xiaotai_starter_runtime_dir}/include"
        "${xiaotai_starter_runtime_dir}/../../../../product/include"
    REQUIRES json
    PRIV_REQUIRES
        nvs_flash
        esp_timer
        esp_system
        freertos
        platform_client
        runtime_config
        starter_media
        starter_tirtc
)

if(NOT CMAKE_BUILD_EARLY_EXPANSION)
    idf_build_get_property(firmware_identity PROJECT_VER)
    string(REGEX REPLACE "[+].*$" "" product_version "${firmware_identity}")
    target_compile_definitions(${COMPONENT_LIB} PRIVATE XIAOTAI_PROFILE_VERSION="${product_version}")
endif()
