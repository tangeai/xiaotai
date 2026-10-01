set(XIAOTAI_STARTER_VOICE_MODEL_DIR "${CMAKE_CURRENT_LIST_DIR}/model")
include("${CMAKE_CURRENT_LIST_DIR}/../../cmake/xiaotai_repository.cmake")
xiaotai_find_repository_root(XIAOTAI_REPO_ROOT)
include("${XIAOTAI_REPO_ROOT}/platforms/esp-idf/components/starter_voice/CMakeLists.txt")
