set(TIRTC_ROOT "${CMAKE_CURRENT_LIST_DIR}/../third_party/tirtc")
set(XIAOTAI_REPO_ROOT "${CMAKE_CURRENT_LIST_DIR}")
while(NOT EXISTS "${XIAOTAI_REPO_ROOT}/boards/schema/board.schema.json")
    get_filename_component(XIAOTAI_REPO_PARENT "${XIAOTAI_REPO_ROOT}" DIRECTORY)
    if(XIAOTAI_REPO_PARENT STREQUAL XIAOTAI_REPO_ROOT)
        message(FATAL_ERROR
            "Cannot locate XiaoTai repository root from ${CMAKE_CURRENT_LIST_DIR}")
    endif()
    set(XIAOTAI_REPO_ROOT "${XIAOTAI_REPO_PARENT}")
endwhile()
set(XIAOTAI_PRODUCT_ROOT "${XIAOTAI_REPO_ROOT}/product")
set(XIAOTAI_BOARD_ROOT "${XIAOTAI_REPO_ROOT}/boards/lckfb/bk7258")
set(XIAOTAI_COMMON_ROOT "${XIAOTAI_REPO_ROOT}/platforms/common")

set(srcs
    ap_main.c
    src/xiaotai_app.c
    src/xiaotai_button.c
    "${XIAOTAI_BOARD_ROOT}/board_button.c"
    src/xiaotai_touch.c
    src/xiaotai_touch_policy.c
    "${XIAOTAI_BOARD_ROOT}/board_touch.c"
    src/xiaotai_touch_transform.c
    src/xiaotai_ui_hit_test.c
    "${XIAOTAI_PRODUCT_ROOT}/src/xiaotai_contacts.c"
    "${XIAOTAI_PRODUCT_ROOT}/src/xiaotai_ai_view.c"
    "${XIAOTAI_PRODUCT_ROOT}/src/xiaotai_ai_protocol.c"
    "${XIAOTAI_PRODUCT_ROOT}/src/xiaotai_call_protocol.c"
    "${XIAOTAI_PRODUCT_ROOT}/src/xiaotai_call_state.c"
    "${XIAOTAI_PRODUCT_ROOT}/src/xiaotai_room.c"
    "${XIAOTAI_PRODUCT_ROOT}/src/xiaotai_runtime.c"
    "${XIAOTAI_PRODUCT_ROOT}/src/xiaotai_signal.c"
    src/xiaotai_tirtc.c
    src/xiaotai_tirtc_sdk_compat.c
    src/xiaotai_tirtc_recovery.c
    src/xiaotai_bk_wifi_hooks.c
    src/xiaotai_storage.c
    src/xiaotai_network.c
    src/xiaotai_network_state.c
    src/xiaotai_metrics.c
    src/xiaotai_log.c
    src/xiaotai_platform_client.c
    src/xiaotai_pcm_stream.c
    src/xiaotai_console.c
    src/xiaotai_time.c
    src/xiaotai_audio.c
    src/xiaotai_audio_agc.c
    src/xiaotai_audio_ns.c
    src/xiaotai_audio_policy.c
    "${XIAOTAI_BOARD_ROOT}/board_audio.c"
    "${XIAOTAI_PRODUCT_ROOT}/src/xiaotai_video_pacer.c"
    src/xiaotai_video.c
    src/xiaotai_ui.c
    "${XIAOTAI_BOARD_ROOT}/board_camera.c"
    "${XIAOTAI_BOARD_ROOT}/board_display.c"
    src/tirtc_bk_probe.c
    src/xiaotai_probe.c
    assets/xiaotai_ui_assets.c
)

set(incs
    .
    include
    assets
    "${XIAOTAI_PRODUCT_ROOT}/include"
    "${XIAOTAI_PRODUCT_ROOT}/assets/icons/wechat_call"
    "${XIAOTAI_COMMON_ROOT}/include"
    "${XIAOTAI_BOARD_ROOT}"
    "${TIRTC_ROOT}/include"
)

set(priv_req
    bk_common
    bk_rtos
    bk_vfs
    bk_wifi_driver
    bk_wifi
    bk_netif
    lwip_intf_v2_1
    easy_flash
    json
    media_service
    multimedia
    audio_record
    audio_play
    webclient
    bk_display
    bk_cli
    bk_peripheral
    hmac_sha_256
    psa_mbedtls
    zlib
)

armino_component_register(
    SRCS "${srcs}"
    INCLUDE_DIRS "${incs}"
    PRIV_REQUIRES "${priv_req}"
)

add_prebuilt_library(tirtc_bk7258
                     "${TIRTC_ROOT}/lib/libTiRTC.a")
target_link_libraries(${COMPONENT_LIB} INTERFACE
    "-Wl,--whole-archive"
    tirtc_bk7258
    "-Wl,--no-whole-archive"
)
