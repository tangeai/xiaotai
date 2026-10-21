include("${CMAKE_CURRENT_LIST_DIR}/../../cmake/xiaotai_repository.cmake")
xiaotai_find_repository_root(XIAOTAI_REPO_ROOT)
set(board_dir "${XIAOTAI_REPO_ROOT}/boards/m5stack/cores3-lite")
set(XIAOTAI_STARTER_PRODUCT_EXTRA_REQUIRES esp_lcd_ili9341)
# Matches the board variant layout profile; link exactly one full CJK font.
set(XIAOTAI_STARTER_PRODUCT_LAYOUT_PROFILE compact)
set(XIAOTAI_STARTER_PRODUCT_ASSET_DIR "${XIAOTAI_REPO_ROOT}/product/assets/audio")
set(XIAOTAI_STARTER_PRODUCT_EXTRA_SRCS "${board_dir}/board_display.c")
set(XIAOTAI_STARTER_PRODUCT_EXTRA_PRIV_INCLUDE_DIRS "${board_dir}")
include("${XIAOTAI_REPO_ROOT}/platforms/esp-idf/components/starter_product/CMakeLists.txt")

if(NOT CMAKE_BUILD_EARLY_EXPANSION)
    # Only the two bounded caption arrays; do not move the whole component BSS.
    target_compile_definitions(${COMPONENT_LIB} PRIVATE XIAOTAI_UI_TEXT_PSRAM=1)
endif()

if(NOT CMAKE_BUILD_EARLY_EXPANSION AND XIAOTAI_BOARD_VARIANT STREQUAL "ili9342c")
    target_compile_definitions(${COMPONENT_LIB} PRIVATE XIAOTAI_PANEL_ILI9342C=1)
endif()
