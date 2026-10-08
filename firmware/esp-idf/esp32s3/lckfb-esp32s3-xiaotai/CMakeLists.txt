cmake_minimum_required(VERSION 3.16)

include("${CMAKE_CURRENT_LIST_DIR}/cmake/xiaotai_repository.cmake")
xiaotai_find_repository_root(XIAOTAI_REPO_ROOT)

# Diagnostic firmware identity. Keep this explicit so ESP-IDF embeds the same
# human-readable version in the app descriptor and bootloader startup output.
set(PROJECT_VER "1.0.0+build.14")

# 生成器把 SDK 复制到 third_party/tirtc，使生成工程可以离开本仓库独立构建。
# 高级用户仍可通过 -DTIRTC_SDK_DIR=<path> 指向另一份兼容 SDK。
set(TIRTC_SDK_DIR "${CMAKE_CURRENT_LIST_DIR}/third_party/tirtc"
    CACHE PATH "TiRTC ESP32-S3 SDK root")

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(lckfb_esp32s3_xiaotai)

# BEGIN JPEG PERFORMANCE
include("${CMAKE_CURRENT_LIST_DIR}/cmake/xiaotai_camera_performance.cmake")
xiaotai_optimize_camera_jpeg()
# END JPEG PERFORMANCE

# This Kconfig symbol is hidden when trace support is off, so confgen omits its
# explicit "not set" line even though the value is correct. Device Kit Doctor
# requires all ABI contract keys to remain explicit in the generated sdkconfig.
set(tirtc_stats_marker "# CONFIG_FREERTOS_USE_STATS_FORMATTING_FUNCTIONS is not set")
file(READ "${CMAKE_CURRENT_LIST_DIR}/sdkconfig" tirtc_sdkconfig_text)
string(FIND "${tirtc_sdkconfig_text}" "${tirtc_stats_marker}" tirtc_stats_marker_pos)
if(tirtc_stats_marker_pos EQUAL -1)
    file(APPEND "${CMAKE_CURRENT_LIST_DIR}/sdkconfig" "\n${tirtc_stats_marker}\n")
endif()

# Guard the boot boundary: ESP-IDF aborts before app_main when both I2C driver
# families are linked into one image.
add_custom_command(
    TARGET ${CMAKE_PROJECT_NAME}.elf
    POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E env CROSS_NM=${CMAKE_NM}
            bash ${XIAOTAI_REPO_ROOT}/tools/check_esp_idf_i2c_driver_family.sh
            $<TARGET_FILE:${CMAKE_PROJECT_NAME}.elf>
    COMMAND bash ${CMAKE_CURRENT_LIST_DIR}/tools/check_product_touch_i2c_policy.sh
            ${XIAOTAI_REPO_ROOT}/platforms/esp-idf/components/starter_product/src/starter_product.c
    COMMAND bash ${CMAKE_CURRENT_LIST_DIR}/tools/check_backlight_polarity_policy.sh
            ${XIAOTAI_REPO_ROOT}/platforms/esp-idf/components/starter_product/src/starter_product.c
    COMMAND bash ${CMAKE_CURRENT_LIST_DIR}/tools/check_binding_prompt_policy.sh
            ${CMAKE_CURRENT_LIST_DIR}
    COMMAND bash ${CMAKE_CURRENT_LIST_DIR}/tools/check_wifi_provisioning_policy.sh
            ${CMAKE_CURRENT_LIST_DIR}
    COMMAND bash ${CMAKE_CURRENT_LIST_DIR}/tools/check_product_ui_policy.sh
            ${XIAOTAI_REPO_ROOT}/platforms/esp-idf/components/starter_product/src/starter_product.c
    COMMAND bash ${CMAKE_CURRENT_LIST_DIR}/tools/check_time_sync_policy.sh
            ${CMAKE_CURRENT_LIST_DIR}
    COMMAND bash ${CMAKE_CURRENT_LIST_DIR}/tools/check_tirtc_startup_order_policy.sh
            ${CMAKE_CURRENT_LIST_DIR}
    COMMAND bash ${CMAKE_CURRENT_LIST_DIR}/tools/check_realtime_connect_memory_policy.sh
            ${CMAKE_CURRENT_LIST_DIR}
    COMMAND ${CMAKE_COMMAND} -E env CROSS_NM=${CMAKE_NM}
            bash ${CMAKE_CURRENT_LIST_DIR}/tools/check_tirtc_thread_stack_policy.sh
            ${CMAKE_CURRENT_LIST_DIR} $<TARGET_FILE:${CMAKE_PROJECT_NAME}.elf>
    COMMAND bash ${CMAKE_CURRENT_LIST_DIR}/tools/check_voice_intent_policy.sh
            ${CMAKE_CURRENT_LIST_DIR}
    COMMAND ${CMAKE_COMMAND} -E env CROSS_NM=${CMAKE_NM}
            bash ${CMAKE_CURRENT_LIST_DIR}/tools/check_wake_image_policy.sh
            $<TARGET_FILE:${CMAKE_PROJECT_NAME}.elf>
    COMMAND bash ${CMAKE_CURRENT_LIST_DIR}/tools/check_session_priority_policy.sh
            ${CMAKE_CURRENT_LIST_DIR}
    COMMAND bash ${CMAKE_CURRENT_LIST_DIR}/tools/test_camera_sensor_policy.sh
    COMMAND bash ${CMAKE_CURRENT_LIST_DIR}/tools/check_camera_realtime_policy.sh
    COMMAND bash ${CMAKE_CURRENT_LIST_DIR}/tools/check_media_cpu_fairness_policy.sh
    COMMAND bash ${CMAKE_CURRENT_LIST_DIR}/tools/check_i2s_resource_policy.sh
    COMMAND bash ${CMAKE_CURRENT_LIST_DIR}/tools/check_audio_capture_policy.sh
    COMMAND bash ${CMAKE_CURRENT_LIST_DIR}/tools/check_aec_policy.sh
    COMMAND bash ${CMAKE_CURRENT_LIST_DIR}/tools/check_downlink_audio_policy.sh
            ${CMAKE_CURRENT_LIST_DIR}
    COMMAND bash ${CMAKE_CURRENT_LIST_DIR}/tools/check_memory_placement_policy.sh
    COMMAND bash ${CMAKE_CURRENT_LIST_DIR}/tools/test_jpeg_collector.sh
    COMMAND ${CMAKE_COMMAND} -E env CROSS_OBJDUMP=${CMAKE_OBJDUMP}
            bash ${CMAKE_CURRENT_LIST_DIR}/tools/check_ai_stack_policy.sh
            $<TARGET_FILE:${CMAKE_PROJECT_NAME}.elf>
    VERBATIM
)

# A changed policy must be re-run even if no firmware source changed.
file(GLOB policy_scripts CONFIGURE_DEPENDS "${CMAKE_CURRENT_LIST_DIR}/tools/*.sh")
set_property(TARGET ${CMAKE_PROJECT_NAME}.elf APPEND PROPERTY LINK_DEPENDS ${policy_scripts})
