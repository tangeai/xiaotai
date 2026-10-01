set(xiaotai_starter_tirtc_dir "${CMAKE_CURRENT_LIST_DIR}")

idf_component_register(
    SRCS "${xiaotai_starter_tirtc_dir}/src/starter_tirtc.c"
    INCLUDE_DIRS "${xiaotai_starter_tirtc_dir}/include"
    REQUIRES tirtc_sdk freertos
    PRIV_REQUIRES log
)
