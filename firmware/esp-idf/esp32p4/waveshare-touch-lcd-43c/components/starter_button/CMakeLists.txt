include("${CMAKE_CURRENT_LIST_DIR}/../../cmake/xiaotai_repository.cmake")
xiaotai_find_repository_root(XIAOTAI_REPO_ROOT)
set(XIAOTAI_BOARD_BUTTON_INCLUDE_DIR
    "${XIAOTAI_REPO_ROOT}/boards/waveshare/esp32p4-touch-lcd-43c")
set(XIAOTAI_BOARD_BUTTON_SOURCE
    "${XIAOTAI_REPO_ROOT}/platforms/esp-idf/waveshare_p4/button_adapter.c")
include("${XIAOTAI_REPO_ROOT}/platforms/esp-idf/components/starter_button/CMakeLists.txt")
