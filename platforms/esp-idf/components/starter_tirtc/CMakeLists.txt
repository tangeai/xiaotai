set(xiaotai_starter_tirtc_dir "${CMAKE_CURRENT_LIST_DIR}")

set(audio_trace_sources)
if(CONFIG_STARTER_AUDIO_PACKET_TRACE)
    list(APPEND audio_trace_sources "${xiaotai_starter_tirtc_dir}/src/starter_audio_trace.c")
endif()

idf_component_register(
    SRCS "${xiaotai_starter_tirtc_dir}/src/starter_tirtc.c"
         "${xiaotai_starter_tirtc_dir}/src/starter_pipeline_diag.c"
         ${audio_trace_sources}
    INCLUDE_DIRS "${xiaotai_starter_tirtc_dir}/include"
    PRIV_INCLUDE_DIRS "${xiaotai_starter_tirtc_dir}/../../../common/tirtc"
    REQUIRES tirtc_sdk freertos
    PRIV_REQUIRES log esp_timer lwip
)

if(CONFIG_STARTER_PIPELINE_DIAGNOSTICS)
    target_link_libraries(${COMPONENT_LIB} INTERFACE
        "-Wl,--wrap=lwip_send" "-Wl,--wrap=lwip_sendto"
        "-Wl,--wrap=lwip_sendmsg"
        "-Wl,--wrap=lwip_recv" "-Wl,--wrap=lwip_recvfrom")
endif()
