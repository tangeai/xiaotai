include("${CMAKE_CURRENT_LIST_DIR}/../../cmake/xiaotai_repository.cmake")
xiaotai_find_repository_root(XIAOTAI_REPO_ROOT)
# Matches the board variant layout profile; link exactly one full CJK font.
set(XIAOTAI_STARTER_PRODUCT_LAYOUT_PROFILE large)
set(XIAOTAI_STARTER_PRODUCT_ASSET_DIR "${XIAOTAI_REPO_ROOT}/product/assets/audio")
set(XIAOTAI_STARTER_PRODUCT_EXTRA_REQUIRES p4_hardware)
include("${XIAOTAI_REPO_ROOT}/platforms/esp-idf/components/starter_product/CMakeLists.txt")
