# Keep vendor files outside the component so SDK replacement preserves project gates.
get_filename_component(TIRTC_SDK_DIR
    "${CMAKE_CURRENT_LIST_DIR}/../../third_party/tirtc" ABSOLUTE)
foreach(required IN ITEMS include/tirtc/tiRTC.h lib/libTiRTC.a manifest/build-contract.env)
    if(NOT EXISTS "${TIRTC_SDK_DIR}/${required}")
        message(FATAL_ERROR "Incomplete TiRTC P4 SDK: ${TIRTC_SDK_DIR}/${required}")
    endif()
endforeach()

idf_component_register(
    SRCS "src/tirtc_task_stack_policy.c"
    WHOLE_ARCHIVE
    INCLUDE_DIRS "${TIRTC_SDK_DIR}/include" "${TIRTC_SDK_DIR}/include/tirtc"
    REQUIRES freertos esp_http_client esp_netif esp_timer hal json lwip mbedtls soc
)

set(TIRTC_LIB "${TIRTC_SDK_DIR}/lib/libTiRTC.a")

if(NOT EXISTS "${TIRTC_LIB}")
    message(FATAL_ERROR "TiRTC static library not found: ${TIRTC_LIB}")
endif()

# The supplied TiRTC 2.5.0 P4 archive is not interchangeable with the S3 SDK.
if(NOT IDF_TARGET STREQUAL "esp32p4")
    message(FATAL_ERROR "XiaoTai P4 requires the ESP32-P4 target")
endif()
if(NOT IDF_VERSION_MAJOR EQUAL 5 OR NOT IDF_VERSION_MINOR EQUAL 5 OR
   NOT IDF_VERSION_PATCH EQUAL 5)
    message(FATAL_ERROR "The pinned TiRTC P4 baseline requires ESP-IDF 5.5.5")
endif()
if(NOT CONFIG_FREERTOS_HZ EQUAL 1000 OR
   CONFIG_FREERTOS_USE_TRACE_FACILITY OR CONFIG_FREERTOS_GENERATE_RUN_TIME_STATS OR
   CONFIG_FREERTOS_USE_STATS_FORMATTING_FUNCTIONS)
    message(FATAL_ERROR "TiRTC P4 requires 1000 Hz ticks and disabled trace/runtime stats")
endif()
file(SHA256 "${TIRTC_LIB}" TIRTC_LIB_SHA256)
if(NOT CONFIG_MBEDTLS_CERTIFICATE_BUNDLE OR NOT CONFIG_LWIP_MAX_SOCKETS EQUAL 16)
    message(FATAL_ERROR "P4 fixes2 SDK requires the certificate bundle and 16 application sockets")
endif()
if(NOT TIRTC_LIB_SHA256 STREQUAL
   "6c5c1543f4a099c2f8fdfdff5b50ef728cf181ce09931c2798d34e84d22473b1")
    message(FATAL_ERROR "TiRTC archive differs from the pinned P4 baseline; review its ABI before updating the contract")
endif()

add_library(tirtc_single_prebuilt STATIC IMPORTED)
file(STRINGS "${TIRTC_SDK_DIR}/manifest/build-contract.env"
    TIRTC_SEMAPHORE_CONTRACT REGEX "^STATIC_SEMAPHORE_T_SIZE=[0-9]+$")
if(NOT TIRTC_SEMAPHORE_CONTRACT STREQUAL "STATIC_SEMAPHORE_T_SIZE=84")
    message(FATAL_ERROR "Pinned P4 archive requires an 84-byte StaticSemaphore_t (see manifest/build-contract.env)")
endif()
target_compile_definitions(${COMPONENT_LIB} INTERFACE TIRTC_SDK_STATIC_SEMAPHORE_SIZE=84)
set_target_properties(tirtc_single_prebuilt PROPERTIES IMPORTED_LOCATION "${TIRTC_LIB}")

# libwebrtc_nosctp.a is bundled into libTiRTC.a; the app links this SDK archive only.
add_library(tirtc_link_bundle INTERFACE)
target_link_libraries(tirtc_link_bundle INTERFACE
    "-Wl,--start-group"
    tirtc_single_prebuilt
    $<TARGET_FILE:__idf_lwip>
    $<TARGET_FILE:__idf_mbedtls>
    $<TARGET_FILE:mbedcrypto>
    $<TARGET_FILE:mbedtls>
    $<TARGET_FILE:mbedx509>
    $<TARGET_FILE:everest>
    $<TARGET_FILE:p256m>
    $<TARGET_FILE:__idf_hal>
    $<TARGET_FILE:__idf_soc>
    "-Wl,--end-group")

target_link_libraries(${COMPONENT_LIB} INTERFACE tirtc_link_bundle)

# Preserve SDK ABI while raising only the rtc_thread minimum stack budget.
target_link_libraries(${COMPONENT_LIB} INTERFACE
    "-Wl,--wrap=freertos_ThreadCreateWithStackSize")
