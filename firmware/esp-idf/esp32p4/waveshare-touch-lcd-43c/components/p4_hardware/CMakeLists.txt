set(ref "${CMAKE_CURRENT_LIST_DIR}/../../main")
include("${CMAKE_CURRENT_LIST_DIR}/../../cmake/xiaotai_repository.cmake")
xiaotai_find_repository_root(XIAOTAI_REPO_ROOT)
set(xiaotai_platform "${XIAOTAI_REPO_ROOT}/platforms/esp-idf/waveshare_p4")
include("${xiaotai_platform}/component_sources.cmake")
idf_component_register(
 SRCS "p4_video.c" ${XIAOTAI_P4_FAMILY_SRCS} ${XIAOTAI_P4_PROJECT_MEDIA_SRCS}
 INCLUDE_DIRS "." ${XIAOTAI_P4_INCLUDE_DIRS}
 PRIV_INCLUDE_DIRS ${XIAOTAI_P4_PRIVATE_INCLUDE_DIRS}
 REQUIRES esp32_p4_wifi6_touch_lcd_4_3 lvgl esp_lvgl_port esp_driver_i2c esp_driver_i2s esp_driver_spi
          esp_driver_ledc starter_tirtc
 PRIV_REQUIRES console esp_video esp_h264 esp_timer esp_driver_ppa esp_driver_jpeg esp_codec_dev)
