include("${CMAKE_CURRENT_LIST_DIR}/../../cmake/xiaotai_repository.cmake")
xiaotai_find_repository_root(XIAOTAI_REPO_ROOT)
set(board_dir "${XIAOTAI_REPO_ROOT}/boards/alientek/atk-dnesp32s3")
idf_component_register(
    SRCS "src/starter_media.c" "${board_dir}/atk_board.c"
    INCLUDE_DIRS "include" "assets"
    PRIV_INCLUDE_DIRS "${board_dir}" "${XIAOTAI_REPO_ROOT}/platforms/common/include"
    REQUIRES starter_tirtc
    PRIV_REQUIRES freertos log esp_driver_i2c esp_driver_i2s esp_driver_gpio esp32-camera esp_timer
    EMBED_FILES "assets/binding_digits.pcm"
)
