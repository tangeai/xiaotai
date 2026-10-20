include("${CMAKE_CURRENT_LIST_DIR}/../../cmake/xiaotai_repository.cmake")
xiaotai_find_repository_root(XIAOTAI_REPO_ROOT)
idf_component_register(
    SRCS "${XIAOTAI_REPO_ROOT}/boards/m5stack/cores3-lite/board_button.c"
    INCLUDE_DIRS "${XIAOTAI_REPO_ROOT}/platforms/esp-idf/components/starter_button/include")
