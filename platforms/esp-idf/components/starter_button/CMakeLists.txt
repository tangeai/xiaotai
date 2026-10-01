if(NOT DEFINED XIAOTAI_BOARD_BUTTON_SOURCE OR
   NOT DEFINED XIAOTAI_BOARD_BUTTON_INCLUDE_DIR)
    message(FATAL_ERROR "starter_button requires a selected board button adapter")
endif()

set(xiaotai_starter_button_dir "${CMAKE_CURRENT_LIST_DIR}")
set(xiaotai_common_include "${CMAKE_CURRENT_LIST_DIR}/../../../common/include")

idf_component_register(
    SRCS
        "${xiaotai_starter_button_dir}/src/starter_button.c"
        "${XIAOTAI_BOARD_BUTTON_SOURCE}"
    INCLUDE_DIRS "${xiaotai_starter_button_dir}/include"
    PRIV_INCLUDE_DIRS
        "${xiaotai_common_include}"
        "${XIAOTAI_BOARD_BUTTON_INCLUDE_DIR}"
    PRIV_REQUIRES
        esp_driver_gpio
        esp_driver_i2c
        esp_driver_i2s
        esp_driver_ledc
        esp_driver_spi
        freertos
        log
        starter_runtime
)
