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
set(XIAOTAI_COMMON_ROOT "${XIAOTAI_REPO_ROOT}/platforms/common")
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
             "${XIAOTAI_VERSION_FILE}")
file(READ "${XIAOTAI_VERSION_FILE}" XIAOTAI_VERSION_TEXT)
string(REGEX MATCH "`([0-9]+\\.[0-9]+\\.[0-9]+)`"
       XIAOTAI_VERSION_MATCH "${XIAOTAI_VERSION_TEXT}")
if(NOT XIAOTAI_VERSION_MATCH)
    message(FATAL_ERROR "VERSION.md must contain a major.minor.patch version")
endif()
set(XIAOTAI_FIRMWARE_VERSION "${CMAKE_MATCH_1}")
message(STATUS "XiaoTai firmware version: ${XIAOTAI_FIRMWARE_VERSION}")

set(srcs
    "${XIAOTAI_NATIVE_AP_ROOT}/ap_main.c"
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
    "${XIAOTAI_COMMON_ROOT}/src/starter_audio_resampler.c"
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
    "${XIAOTAI_NATIVE_AP_ROOT}/tirtc_bk_probe.c"
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

# Platform runtime is shared; native entries and all peripherals are board-selected.
if(XIAOTAI_HEADLESS)
    list(REMOVE_ITEM srcs src/xiaotai_ui.c src/xiaotai_button.c
         "${XIAOTAI_NATIVE_AP_ROOT}/tirtc_bk_probe.c" src/xiaotai_touch.c src/xiaotai_touch_policy.c
         src/xiaotai_touch_transform.c "${XIAOTAI_BOARD_ROOT}/board_touch.c"
         "${XIAOTAI_BOARD_ROOT}/board_display.c" assets/xiaotai_ui_assets.c)
    list(APPEND srcs "${XIAOTAI_BOARD_ROOT}/board_indicator.c"
         "${XIAOTAI_NATIVE_AP_ROOT}/probe.c"
         "${XIAOTAI_REPO_ROOT}/platforms/beken/src/xiaotai_headless_ui.c"
         "${XIAOTAI_REPO_ROOT}/platforms/beken/src/xiaotai_key_input.c"
         "${XIAOTAI_REPO_ROOT}/product/interaction/src/xiaotai_key_gesture.c")
    list(APPEND incs "${XIAOTAI_REPO_ROOT}/product/interaction/include")
    list(REMOVE_ITEM priv_req bk_display)
endif()
list(APPEND srcs "${XIAOTAI_REPO_ROOT}/platforms/beken/src/xiaotai_onboard_audio.c")
set(resolved_srcs)
foreach(source IN LISTS srcs)
    if(NOT IS_ABSOLUTE "${source}")
        set(source "${CMAKE_CURRENT_LIST_DIR}/${source}")
    endif()
    list(APPEND resolved_srcs "${source}")
endforeach()
set(srcs "${resolved_srcs}")
set(resolved_incs)
foreach(dir IN LISTS incs)
    if(NOT IS_ABSOLUTE "${dir}")
        set(dir "${CMAKE_CURRENT_LIST_DIR}/${dir}")
    endif()
    list(APPEND resolved_incs "${dir}")
endforeach()
set(incs "${resolved_incs}")

armino_component_register(
    SRCS "${srcs}"
    INCLUDE_DIRS "${incs}"
    PRIV_REQUIRES "${priv_req}"
)
target_compile_definitions(${COMPONENT_LIB} PRIVATE
    XIAOTAI_FIRMWARE_VERSION=\"${XIAOTAI_FIRMWARE_VERSION}\")
get_filename_component(identity_dir "${CMAKE_BINARY_DIR}/.." ABSOLUTE)
if(NOT EXISTS "${identity_dir}/xiaotai_build_identity.h")
    message(FATAL_ERROR "Missing build identity; invoke tools/build.sh")
endif()
target_include_directories(${COMPONENT_LIB} PRIVATE "${identity_dir}")

add_prebuilt_library(tirtc_bk7258
                     "${TIRTC_ROOT}/lib/libTiRTC.a")
target_link_libraries(${COMPONENT_LIB} INTERFACE
    "-Wl,--whole-archive"
    tirtc_bk7258
    "-Wl,--no-whole-archive"
)

if(XIAOTAI_HEADLESS)
    target_compile_definitions(${COMPONENT_LIB} PRIVATE
        XIAOTAI_HEADLESS=1 ${XIAOTAI_FEATURE_DEFINITIONS}
        XIAOTAI_BOARD_ID="${XIAOTAI_BOARD_ID}")
endif()

if(XIAOTAI_DUAL_EYES)
    target_sources(${COMPONENT_LIB} PRIVATE
        "${XIAOTAI_BOARD_ROOT}/board_eyes.c"
        "${XIAOTAI_REPO_ROOT}/platforms/beken/src/xiaotai_eyes_ui.c"
        "${XIAOTAI_PRODUCT_ROOT}/src/xiaotai_eyes.c"
        "${XIAOTAI_PRODUCT_ROOT}/src/xiaotai_eye_text.c"
        "${CMAKE_CURRENT_LIST_DIR}/assets/xiaotai_ui_assets.c")
    target_compile_definitions(${COMPONENT_LIB} PRIVATE XIAOTAI_DUAL_EYES=1)
endif()
