# TiRTC ESP32-S3 SDK 接入说明

## 使用前确认

- 平台基线：ESP32-S3、ESP-IDF 5.5.4、`xtensa-esp32s3-elf-gcc 14.2.0_20260121`。
- 本包是 Mini/KCP 静态 SDK，不提供 SCTP；`TiRtcConnect()` 或 `TiRtcWhipConnect()` 的目标必须支持 TiRTC/TGTRP KCP，不能把它作为标准浏览器 WebRTC SDK 使用。
- FreeRTOS ABI：`CONFIG_FREERTOS_HZ=1000`，trace、统计格式化和运行统计均关闭，`sizeof(StaticSemaphore_t)=84`。
- 内存基线：PSRAM 必须成功初始化，并启用 PSRAM malloc 和外部任务栈；PSRAM 的 Quad/Octal 模式、电压、频率与容量按实际模组配置。
- 联网状态：使用 ESP32-S3 原生 Wi-Fi；调用 `TiRtcStart()` 前必须已收到 `IP_EVENT_STA_GOT_IP`，并确认默认路由、DNS 和系统时间可用。`WIFI_EVENT_STA_CONNECTED` 只表示已关联 AP，不能作为 TiRTC 启动条件。
- `TIRTC_OPT_SERVICE_ENDPOINT` 可使用 HTTP 或 HTTPS。HTTPS 由 ESP-IDF 5.5.4 的 mbedTLS 提供，工程必须保留 TLS 客户端和 TLS 1.2 能力。

## 工程集成

把完整 SDK 放入独立 ESP-IDF 组件；头文件与 `libTiRTC.a` 必须来自同一个 ESP32-S3 包：

```text
components/tirtc_sdk/
├── CMakeLists.txt
├── include/tirtc/
│   ├── basedef.h
│   ├── tgtrp.h
│   ├── tiRTC.h
│   ├── tiRTC_stat.h
│   └── ticloudstorage.h
└── lib/libTiRTC.a
```

`components/tirtc_sdk/CMakeLists.txt`：

```cmake
idf_component_register(
    INCLUDE_DIRS "include"
    REQUIRES freertos esp_http_client esp_netif esp_timer hal json lwip mbedtls soc
)

set(TIRTC_LIB "${CMAKE_CURRENT_LIST_DIR}/lib/libTiRTC.a")
if(NOT EXISTS "${TIRTC_LIB}")
    message(FATAL_ERROR "TiRTC static library not found: ${TIRTC_LIB}")
endif()

add_library(tirtc_prebuilt STATIC IMPORTED)
set_target_properties(tirtc_prebuilt PROPERTIES IMPORTED_LOCATION "${TIRTC_LIB}")

add_library(tirtc_link_bundle INTERFACE)
target_link_libraries(tirtc_link_bundle INTERFACE
    "-Wl,--start-group"
    tirtc_prebuilt
    $<TARGET_FILE:__idf_lwip>
    $<TARGET_FILE:__idf_mbedtls>
    $<TARGET_FILE:mbedcrypto>
    $<TARGET_FILE:mbedtls>
    $<TARGET_FILE:mbedx509>
    $<TARGET_FILE:everest>
    $<TARGET_FILE:p256m>
    $<TARGET_FILE:__idf_hal>
    $<TARGET_FILE:__idf_soc>
    "-Wl,--end-group"
)
target_link_libraries(${COMPONENT_LIB} INTERFACE tirtc_link_bundle)
```

应用组件在 `REQUIRES` 中增加 `tirtc_sdk`，源码使用
`#include <tirtc/tiRTC.h>`；需要云存储能力时再包含
`#include <tirtc/ticloudstorage.h>`。`libTiRTC.a` 已包含本平台配套的 TGTRP
KCP 实现，不要再链接其他版本、其他芯片的 TiRTC/TGTRP 归档。

最终工程需保持以下配置：

```text
CONFIG_FREERTOS_HZ=1000
# CONFIG_FREERTOS_USE_TRACE_FACILITY is not set
# CONFIG_FREERTOS_USE_STATS_FORMATTING_FUNCTIONS is not set
# CONFIG_FREERTOS_GENERATE_RUN_TIME_STATS is not set
CONFIG_SPIRAM=y
CONFIG_SPIRAM_USE_MALLOC=y
CONFIG_FREERTOS_TASK_CREATE_ALLOW_EXT_MEM=y
CONFIG_MBEDTLS_TLS_SERVER_AND_CLIENT=y
CONFIG_MBEDTLS_SSL_PROTO_TLS1_2=y
```

建议在应用组件加入 ABI 编译期检查：

```c
#include "freertos/semphr.h"

#if CONFIG_FREERTOS_HZ != 1000
#error "TiRTC ESP32-S3 requires a 1000 Hz FreeRTOS tick"
#endif
#ifdef CONFIG_FREERTOS_USE_TRACE_FACILITY
#error "TiRTC ESP32-S3 requires FreeRTOS trace disabled"
#endif
#ifdef CONFIG_FREERTOS_USE_STATS_FORMATTING_FUNCTIONS
#error "TiRTC ESP32-S3 requires FreeRTOS stats formatting disabled"
#endif
#ifdef CONFIG_FREERTOS_GENERATE_RUN_TIME_STATS
#error "TiRTC ESP32-S3 requires FreeRTOS run-time stats disabled"
#endif
_Static_assert(sizeof(StaticSemaphore_t) == 84,
               "TiRTC ESP32-S3 FreeRTOS ABI mismatch");
```

更换 SDK 或配置后执行 `idf.py fullclean`、`idf.py reconfigure` 和
`idf.py build`。以最终的 `build/config/sdkconfig.h` 与包内
`manifest/build-contract.env` 为准，不要只检查 `sdkconfig.defaults`。

## 启动、内存与媒体任务

- 推荐顺序：PSRAM 初始化成功 → Wi-Fi 启动 → `IP_EVENT_STA_GOT_IP` → 校准系统时间 → `TiRtcInit / TiRtcSetOption / TiRtcStart` → `TIRTC_EVENT_SYS_STARTED`。`TiRtcStart()` 返回 `0` 只表示异步启动请求已受理。
- Wi-Fi/IP 事件回调只设置 EventGroup 或投递轻量消息，由应用任务执行时间同步和 TiRTC 启动，不要阻塞 ESP-IDF 系统事件线程。
- 大媒体帧、编码输出、接收帧副本和固定帧池适合 PSRAM；DMA 描述符、`MALLOC_CAP_DMA` 缓冲、同步对象和小型控制消息应保留在内部 RAM。排查内存时同时观察内部 RAM 最低水位和最大连续空闲块。
- SDK 回调中的帧数据只在回调期间有效，异步处理前必须复制到有上限的应用缓冲区。采集、编解码、文件/Flash 操作和长锁不得放在 SDK 回调线程中。
- 媒体任务使用有界队列；发送返回 `TIRTC_E_BUSY` 时应限流或丢弃后续非关键帧，避免积压旧帧。
- 低延迟联调时可临时调用 `esp_wifi_set_ps(WIFI_PS_NONE)` 排除 Wi-Fi 省电抖动；该设置会增加功耗，链路跑通后应按产品目标重新评估。

## 包内容

- `include/tirtc/`：TiRTC 公共头文件。
- `lib/libTiRTC.a`：ESP32-S3 Xtensa 静态库，已合入该平台 TGTRP KCP 实现。
- `manifest/build-contract.env`：FreeRTOS/ESP-IDF ABI 合同，接入工程必须逐项核对。

## 常见集成问题

### 链接阶段出现 undefined reference

确认使用完整 ESP32-S3 包，组件声明包含示例中的 ESP-IDF 依赖和显式链接组，并清理旧构建缓存；不要混入 P4、C3 或其他版本的静态库。

### SDK 线程启动即崩溃

核对 `manifest/build-contract.env` 与最终 `build/config/sdkconfig.h`，尤其是 FreeRTOS tick、trace/stats 和 `StaticSemaphore_t`；同时确认 PSRAM 初始化成功。

### Wi-Fi 已连接但设备无法上线

确认等待的是 `IP_EVENT_STA_GOT_IP`，而不是 `WIFI_EVENT_STA_CONNECTED`；继续检查默认路由、DNS、系统时间和 `TIRTC_EVENT_SYS_STARTED`。

### HTTPS 请求失败

检查 `CONFIG_MBEDTLS_TLS_SERVER_AND_CLIENT`、`CONFIG_MBEDTLS_SSL_PROTO_TLS1_2`、DNS 和系统时间，不要用改回 HTTP 的方式掩盖 TLS 配置问题。

### 出现 `TIRTC_E_BUSY`、卡顿或 DMA 内存不足

检查回调是否执行耗时操作、媒体队列是否无界增长、大缓冲是否错误占用内部 RAM，以及内部连续 DMA 块是否被摄像头/I2S/显示任务耗尽。
