cmake_minimum_required(VERSION 3.16)
include("${CMAKE_CURRENT_LIST_DIR}/cmake/xiaotai_repository.cmake")
xiaotai_find_repository_root(XIAOTAI_REPO_ROOT)
set(PROJECT_VER "1.1.0")
include("${XIAOTAI_REPO_ROOT}/platforms/esp-idf/cmake/xiaotai_build_version.cmake")
set(XIAOTAI_BOARD_VARIANT "ili9342e" CACHE STRING "CoreS3-Lite LCD revision")
if(NOT XIAOTAI_BOARD_VARIANT MATCHES "^(ili9342c|ili9342e)$")
    message(FATAL_ERROR "Select ili9342c or ili9342e")
endif()
# Migrate only the former board-local default; preserve explicit SDK overrides.
if(TIRTC_SDK_DIR STREQUAL "${CMAKE_CURRENT_LIST_DIR}/third_party/tirtc")
    unset(TIRTC_SDK_DIR CACHE)
    unset(TIRTC_SDK_DIR)
endif()
set(TIRTC_SDK_DIR "${XIAOTAI_REPO_ROOT}/platforms/esp-idf/third_party/tirtc/esp32s3"
    CACHE PATH "TiRTC ESP32-S3 SDK root")
list(APPEND EXTRA_COMPONENT_DIRS "${XIAOTAI_REPO_ROOT}/platforms/esp-idf/components/tirtc_sdk")

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(m5stack_cores3_lite_xiaotai)
include("${CMAKE_CURRENT_LIST_DIR}/cmake/check_memory_config.cmake")
include("${CMAKE_CURRENT_LIST_DIR}/cmake/xiaotai_camera_performance.cmake")
xiaotai_optimize_camera_jpeg()
add_custom_target(cores3_board_contract ALL
    COMMAND "${PYTHON}" "${XIAOTAI_REPO_ROOT}/tools/tests/test_cores3_board.py"
    COMMAND "${PYTHON}" "${XIAOTAI_REPO_ROOT}/tools/tests/test_s3_display_geometry.py"
    COMMENT "Checking CoreS3 panel geometry, power isolation, audio slots and product identity"
    VERBATIM)
add_dependencies(${CMAKE_PROJECT_NAME}.elf cores3_board_contract)
add_custom_command(TARGET ${CMAKE_PROJECT_NAME}.elf POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E env CROSS_NM=${CMAKE_NM}
        bash ${XIAOTAI_REPO_ROOT}/tools/check_esp_idf_i2c_driver_family.sh
        $<TARGET_FILE:${CMAKE_PROJECT_NAME}.elf>
    VERBATIM)
