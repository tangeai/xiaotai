include("${CMAKE_CURRENT_LIST_DIR}/../../cmake/xiaotai_repository.cmake")
xiaotai_find_repository_root(XIAOTAI_REPO_ROOT)
include("${XIAOTAI_REPO_ROOT}/platforms/esp-idf/components/starter_runtime/CMakeLists.txt")
