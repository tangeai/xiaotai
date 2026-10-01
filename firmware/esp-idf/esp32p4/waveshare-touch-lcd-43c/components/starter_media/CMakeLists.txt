include("${CMAKE_CURRENT_LIST_DIR}/../../cmake/xiaotai_repository.cmake")
xiaotai_find_repository_root(XIAOTAI_REPO_ROOT)
set(common "${XIAOTAI_REPO_ROOT}/platforms/common/include")
include("${XIAOTAI_REPO_ROOT}/platforms/esp-idf/components/starter_media_common/CMakeLists.txt")
idf_component_register(
 SRCS "src/starter_media.c" ${XIAOTAI_STARTER_MEDIA_COMMON_SRCS}
 INCLUDE_DIRS "${XIAOTAI_STARTER_MEDIA_COMMON_INCLUDE_DIR}" "${common}"
 REQUIRES starter_tirtc starter_voice esp_driver_i2c p4_hardware
 PRIV_REQUIRES esp_driver_gpio esp_driver_i2s esp_timer freertos log esp_codec_dev esp_audio_codec esp-sr)
