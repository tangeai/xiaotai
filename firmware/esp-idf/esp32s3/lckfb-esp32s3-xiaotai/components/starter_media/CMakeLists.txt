include("${CMAKE_CURRENT_LIST_DIR}/../../cmake/xiaotai_repository.cmake")
xiaotai_find_repository_root(XIAOTAI_REPO_ROOT)
set(board_dir "${XIAOTAI_REPO_ROOT}/boards/lckfb/esp32s3")
include("${XIAOTAI_REPO_ROOT}/platforms/esp-idf/components/starter_media_common/CMakeLists.txt")

idf_component_register(
    SRCS
        ${XIAOTAI_STARTER_MEDIA_COMMON_SRCS}
        "src/starter_jpeg_collector.c"
        "src/starter_media.c"
        "${board_dir}/board_adapter.c"
    INCLUDE_DIRS "include" "${XIAOTAI_STARTER_MEDIA_COMMON_INCLUDE_DIR}"
    PRIV_INCLUDE_DIRS
        "${board_dir}"
        "${XIAOTAI_REPO_ROOT}/platforms/common/include"
    REQUIRES starter_tirtc starter_voice esp_driver_i2c
    PRIV_REQUIRES
        esp_driver_gpio
        esp_driver_i2s
        esp_timer
        freertos
        log
        esp_codec_dev
        esp32-camera
        esp_audio_codec
        esp-sr
)
