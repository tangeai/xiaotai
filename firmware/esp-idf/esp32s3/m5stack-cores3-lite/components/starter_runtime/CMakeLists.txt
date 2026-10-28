include("${CMAKE_CURRENT_LIST_DIR}/../../cmake/xiaotai_repository.cmake")
xiaotai_find_repository_root(XIAOTAI_REPO_ROOT)
include("${XIAOTAI_REPO_ROOT}/platforms/esp-idf/components/starter_runtime/CMakeLists.txt")

if(NOT CMAKE_BUILD_EARLY_EXPANSION)
    target_compile_definitions(${COMPONENT_LIB} PRIVATE XIAOTAI_BOARD_MODEL="m5stack-cores3-lite")
    target_compile_definitions(${COMPONENT_LIB} PRIVATE XIAOTAI_S3_PROFILE_VERSION="1.0.1")
    # Camera at the top; remote players rotate the unmodified camera pixels.
    target_compile_definitions(${COMPONENT_LIB} PRIVATE XIAOTAI_BOARD_CAMERA_ROTATION=180)
endif()
