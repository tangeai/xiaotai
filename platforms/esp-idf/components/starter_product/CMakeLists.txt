if(NOT DEFINED XIAOTAI_STARTER_PRODUCT_ASSET_DIR)
    message(FATAL_ERROR "XIAOTAI_STARTER_PRODUCT_ASSET_DIR must name the selected product asset bundle")
endif()

set(product_assets
    "rings/kids_watch_outgoing_tiny.wav"
    "rings/kids_watch_incoming_tiny.wav"
    "prompts/ai_ack_female_8k.wav"
    "prompts/ai_ack_male_8k.wav"
)
foreach(asset IN LISTS product_assets)
    if(NOT EXISTS "${XIAOTAI_STARTER_PRODUCT_ASSET_DIR}/${asset}")
        message(FATAL_ERROR "starter_product asset missing: ${asset}")
    endif()
endforeach()

idf_component_register(
    SRCS
        "${CMAKE_CURRENT_LIST_DIR}/src/starter_product.c"
        "${CMAKE_CURRENT_LIST_DIR}/src/ui_font_cn_16.c"
        "${CMAKE_CURRENT_LIST_DIR}/src/ui_font_cn_24.c"
        ${XIAOTAI_STARTER_PRODUCT_EXTRA_SRCS}
    INCLUDE_DIRS "${CMAKE_CURRENT_LIST_DIR}/include"
    PRIV_INCLUDE_DIRS ${XIAOTAI_STARTER_PRODUCT_EXTRA_PRIV_INCLUDE_DIRS}
    REQUIRES
        esp_driver_gpio esp_driver_i2c esp_driver_spi esp_lcd nvs_flash
        platform_client runtime_config starter_media starter_runtime starter_voice wifi_manager
        esp_lvgl_port esp_lcd_touch_ft5x06 lvgl esp_app_format
        ${XIAOTAI_STARTER_PRODUCT_EXTRA_REQUIRES}
)

foreach(asset IN LISTS product_assets)
    get_filename_component(asset_name "${asset}" NAME)
    target_add_binary_data(
        ${COMPONENT_LIB}
        "${XIAOTAI_STARTER_PRODUCT_ASSET_DIR}/${asset}"
        BINARY
        RENAME_TO "${asset_name}"
    )
endforeach()
