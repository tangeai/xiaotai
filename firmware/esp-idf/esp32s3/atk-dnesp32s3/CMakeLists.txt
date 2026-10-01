cmake_minimum_required(VERSION 3.16)

# Locate the repository independently of this project's nesting depth.
include("${CMAKE_CURRENT_LIST_DIR}/cmake/xiaotai_repository.cmake")
xiaotai_find_repository_root(XIAOTAI_REPO_ROOT)

# 当前交付使用 Device Kit 附带的 ESP32-S3 2.5.0 SDK。
# 高级用户仍可通过 -DTIRTC_SDK_DIR=<path> 指向另一份兼容 SDK。
set(TIRTC_SDK_DIR "${CMAKE_CURRENT_LIST_DIR}/third_party/tirtc"
    CACHE PATH "TiRTC ESP32-S3 SDK root")

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
set(PROJECT_VER "1.0.0+build.1")
project(atk_xiaotai_s3)

# tirtc-runtime-semantic-gate-v1
add_custom_target(tirtc_runtime_semantic_gate ALL
    COMMAND "${PYTHON}"
            "${CMAKE_CURRENT_LIST_DIR}/tools/verify_runtime_contract.py"
            "${CMAKE_CURRENT_LIST_DIR}/tirtc-runtime-contract.json"
            --project "${CMAKE_CURRENT_LIST_DIR}"
            --evidence-out "${CMAKE_BINARY_DIR}/runtime-contract-evidence.json"
    COMMENT "Validating TiRTC endpoint, callbacks, streams and AI media negotiation"
    VERBATIM
)
add_dependencies(${CMAKE_PROJECT_NAME}.elf tirtc_runtime_semantic_gate)

# tirtc-audio-semantic-gate-v1
add_custom_target(tirtc_audio_semantic_gate ALL
    COMMAND "${PYTHON}"
            "${CMAKE_CURRENT_LIST_DIR}/tools/verify_audio_contract.py"
            "${CMAKE_CURRENT_LIST_DIR}/board-audio-contract.json"
            --project "${CMAKE_CURRENT_LIST_DIR}"
            --evidence-out "${CMAKE_BINARY_DIR}/audio-contract-evidence.json"
    COMMENT "Validating codec clocks, I2S topology, slots and shared-signal handoff"
    VERBATIM
)
add_dependencies(${CMAKE_PROJECT_NAME}.elf tirtc_audio_semantic_gate)

# tirtc-video-semantic-gate-v1
add_custom_target(tirtc_video_semantic_gate ALL
    COMMAND "${PYTHON}"
            "${CMAKE_CURRENT_LIST_DIR}/tools/verify_video_contract.py"
            "${CMAKE_CURRENT_LIST_DIR}/board-video-contract.json"
            --project "${CMAKE_CURRENT_LIST_DIR}"
            --evidence-out "${CMAKE_BINARY_DIR}/video-contract-evidence.json"
    COMMENT "Validating camera lock, CPU isolation, complete JPEG framing, backpressure and memory"
    VERBATIM
)
add_dependencies(${CMAKE_PROJECT_NAME}.elf tirtc_video_semantic_gate)
