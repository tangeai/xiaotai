include("${CMAKE_CURRENT_LIST_DIR}/../../cmake/xiaotai_repository.cmake")
xiaotai_find_repository_root(XIAOTAI_REPO_ROOT)
set(XIAOTAI_BOARD_BUTTON_INCLUDE_DIR
    "${XIAOTAI_REPO_ROOT}/boards/lckfb/esp32s3")
set(XIAOTAI_BOARD_BUTTON_SOURCE
    "${XIAOTAI_BOARD_BUTTON_INCLUDE_DIR}/board_button.c")
include("${XIAOTAI_REPO_ROOT}/platforms/esp-idf/components/starter_button/CMakeLists.txt")
