set(xiaotai_starter_tirtc_dir "${CMAKE_CURRENT_LIST_DIR}")

idf_component_register(
    SRCS "${xiaotai_starter_tirtc_dir}/src/starter_tirtc.c"
         "${xiaotai_starter_tirtc_dir}/src/starter_pipeline_diag.c"
    INCLUDE_DIRS "${xiaotai_starter_tirtc_dir}/include"
    REQUIRES tirtc_sdk freertos
    PRIV_REQUIRES log esp_timer lwip
)

if(CONFIG_STARTER_PIPELINE_DIAGNOSTICS)
    target_link_libraries(${COMPONENT_LIB} INTERFACE
        "-Wl,--wrap=lwip_send" "-Wl,--wrap=lwip_sendto"
        "-Wl,--wrap=lwip_recv" "-Wl,--wrap=lwip_recvfrom")
endif()
