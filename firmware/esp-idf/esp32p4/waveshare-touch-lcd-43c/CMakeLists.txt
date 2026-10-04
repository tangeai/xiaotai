cmake_minimum_required(VERSION 3.16)

include("${CMAKE_CURRENT_LIST_DIR}/cmake/xiaotai_repository.cmake")
xiaotai_find_repository_root(XIAOTAI_REPO_ROOT)

set(PROJECT_VER "1.0.0+build.17")
include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(xiaotai_esp32p4)

add_custom_command(TARGET ${CMAKE_PROJECT_NAME}.elf POST_BUILD
 COMMAND ${CMAKE_COMMAND} -E env CROSS_NM=${CMAKE_NM}
         ${PYTHON} ${CMAKE_CURRENT_LIST_DIR}/tools/check_product_image.py
         $<TARGET_FILE:${CMAKE_PROJECT_NAME}.elf>
 COMMAND ${CMAKE_COMMAND} -E env CROSS_NM=${CMAKE_NM}
         bash ${XIAOTAI_REPO_ROOT}/tools/check_esp_idf_i2c_driver_family.sh
         $<TARGET_FILE:${CMAKE_PROJECT_NAME}.elf>
 VERBATIM)
set_property(TARGET ${CMAKE_PROJECT_NAME}.elf APPEND PROPERTY LINK_DEPENDS
 "${CMAKE_CURRENT_LIST_DIR}/tools/check_product_image.py")

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
