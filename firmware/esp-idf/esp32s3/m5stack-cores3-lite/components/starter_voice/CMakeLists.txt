include("${CMAKE_CURRENT_LIST_DIR}/../../cmake/xiaotai_repository.cmake")
xiaotai_find_repository_root(XIAOTAI_REPO_ROOT)
set(XIAOTAI_STARTER_VOICE_MODEL_DIR "${XIAOTAI_REPO_ROOT}/firmware/esp-idf/esp32s3/lckfb-esp32s3-xiaotai/components/starter_voice/model")
include("${XIAOTAI_REPO_ROOT}/platforms/esp-idf/components/starter_voice/CMakeLists.txt")
