if(NOT DEFINED XIAOTAI_STARTER_VOICE_MODEL_DIR)
    message(FATAL_ERROR "XIAOTAI_STARTER_VOICE_MODEL_DIR must name the selected board model bundle")
endif()

set(model_file "${XIAOTAI_STARTER_VOICE_MODEL_DIR}/nihaoxiaotai.tflite")
if(NOT EXISTS "${model_file}" OR NOT EXISTS "${XIAOTAI_STARTER_VOICE_MODEL_DIR}/head.h")
    message(FATAL_ERROR "starter_voice model bundle requires nihaoxiaotai.tflite and head.h")
endif()

idf_component_register(
    SRCS
        "${CMAKE_CURRENT_LIST_DIR}/src/starter_voice.c"
        "${CMAKE_CURRENT_LIST_DIR}/src/wake_backend.cpp"
        "${CMAKE_CURRENT_LIST_DIR}/vendor/mel_extractor.c"
    INCLUDE_DIRS "${CMAKE_CURRENT_LIST_DIR}/include"
    PRIV_INCLUDE_DIRS
        "${CMAKE_CURRENT_LIST_DIR}/vendor"
        "${CMAKE_CURRENT_LIST_DIR}/src"
        "${XIAOTAI_STARTER_VOICE_MODEL_DIR}"
    REQUIRES esp_common
    PRIV_REQUIRES esp_timer esp-tflite-micro esp-dsp
)
target_add_binary_data(${COMPONENT_LIB} "${model_file}" BINARY RENAME_TO "nihaoxiaotai.tflite")
