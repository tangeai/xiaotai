# Compile authoritative XiaoTai business sources using the P4 toolchain.
# OBJECT only: never link two app_main/session owners into one executable.
# No media/UI stubs; product linking waits for the real board adapters.
include("${CMAKE_CURRENT_LIST_DIR}/../cmake/xiaotai_repository.cmake")
xiaotai_find_repository_root(XIAOTAI_REPO_ROOT)
set(XIAOTAI_PLATFORM_COMPONENTS "${XIAOTAI_REPO_ROOT}/platforms/esp-idf/components")
set(xiaotai_business_sources
    "${XIAOTAI_REPO_ROOT}/product/src/xiaotai_runtime.c"
    "${XIAOTAI_REPO_ROOT}/platforms/esp-idf/main/app_main.c"
    "${XIAOTAI_PLATFORM_COMPONENTS}/platform_client/src/platform_client.c"
    "${XIAOTAI_PLATFORM_COMPONENTS}/runtime_config/src/runtime_config.c"
    "${XIAOTAI_PLATFORM_COMPONENTS}/starter_runtime/src/starter_runtime.c"
    "${XIAOTAI_PLATFORM_COMPONENTS}/starter_tirtc/src/starter_tirtc.c"
    "${XIAOTAI_PLATFORM_COMPONENTS}/starter_console/src/starter_console.c"
    "${XIAOTAI_PLATFORM_COMPONENTS}/wifi_manager/src/wifi_manager.c"
    "${XIAOTAI_PLATFORM_COMPONENTS}/wifi_manager/src/captive_dns.c")
set(xiaotai_business_includes "${XIAOTAI_PLATFORM_COMPONENTS}/wifi_manager/src")
list(APPEND xiaotai_business_includes "${XIAOTAI_REPO_ROOT}/product/include")
foreach(component platform_client runtime_config starter_runtime starter_tirtc
                  starter_console wifi_manager starter_button starter_voice starter_product)
    list(APPEND xiaotai_business_includes
        "${XIAOTAI_PLATFORM_COMPONENTS}/${component}/include")
endforeach()
list(APPEND xiaotai_business_includes
    "${XIAOTAI_PLATFORM_COMPONENTS}/starter_media_common/include")
add_library(xiaotai_business_compile OBJECT ${xiaotai_business_sources})
idf_build_get_property(xiaotai_compile_options COMPILE_OPTIONS)
idf_build_get_property(xiaotai_c_compile_options C_COMPILE_OPTIONS)
idf_build_get_property(xiaotai_compile_definitions COMPILE_DEFINITIONS)
target_compile_options(xiaotai_business_compile PRIVATE
    ${xiaotai_compile_options} ${xiaotai_c_compile_options})
target_compile_definitions(xiaotai_business_compile PRIVATE ${xiaotai_compile_definitions})
target_include_directories(xiaotai_business_compile PRIVATE ${xiaotai_business_includes})
target_link_libraries(xiaotai_business_compile PRIVATE
    idf::esp_app_format idf::esp_system idf::esp_timer idf::esp_driver_i2c
    idf::freertos idf::nvs_flash idf::esp_http_client idf::esp_http_server
    idf::esp_wifi idf::esp_netif idf::esp_event idf::mqtt idf::mbedtls
    idf::json idf::esp-tls idf::lwip idf::console idf::tirtc_sdk)
