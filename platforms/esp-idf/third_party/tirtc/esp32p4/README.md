# TiRTC ESP32-P4 SDK 接入说明

## 使用前确认

- 平台基线：ESP32-P4、ESP-IDF 5.5.4、`riscv32-esp-elf-gcc 14.2.0_20260121`，RISC-V `ilp32f` ABI。
- 本包是 Mini/KCP 静态 SDK，不提供 SCTP/DTLS；`TiRtcConnect()` 或 `TiRtcWhipConnect()` 的目标必须支持 TiRTC/TGTRP KCP，不能连接仅支持标准 WebRTC/浏览器 DTLS/SCTP 的实现。
- FreeRTOS/lwIP ABI：`CONFIG_FREERTOS_HZ=1000`，trace、统计格式化和运行统计均关闭，`sizeof(StaticSemaphore_t)=84`，`CONFIG_LWIP_MAX_SOCKETS=10`。
- ESP32-P4 本身不集成 Wi-Fi。使用 ESP32-C6/C61 无线协处理器时，必须先完成 ESP-Hosted、remote Wi-Fi、SDIO 与从机固件的版本和板级配置。
- 内存基线：PSRAM 必须成功初始化，并启用 PSRAM malloc 和外部任务栈；PSRAM 模式、频率与容量按实际模组配置。
- 调用 `TiRtcStart()` 前，默认 netif 必须已取得 IP，默认路由、DNS 和系统时间可用。
- `TIRTC_OPT_SERVICE_ENDPOINT` 可使用 HTTP 或 HTTPS。HTTPS 由 ESP-IDF 5.5.4 的 mbedTLS 提供，工程必须保留 TLS 客户端和 TLS 1.2 能力。

## 工程集成

典型 P4 无线联网路径如下；TiRTC 只使用应用建立好的 BSD socket/lwIP 默认网络接口，不直接操作 ESP-Hosted 或 SDIO：

```text
TiRTC / BSD socket
  → lwIP / esp_netif
  → esp_wifi_remote
  → ESP-Hosted
  → SDIO
  → ESP32-C6/C61
  → Wi-Fi AP
```

P4 主机工程可在 `idf_component.yml` 中锁定已验证的组件系列：

```yaml
dependencies:
  espressif/esp_hosted:
    version: "==1.4.*"
  espressif/esp_wifi_remote:
    version: "==0.14.*"
```

C6/C61 从机固件必须与所选 ESP-Hosted 版本配套；SDIO GPIO、总线宽度、时钟和复位电平按板级原理图配置，不要照抄其他开发板。

把完整 SDK 放入独立 ESP-IDF 组件；头文件与 `libTiRTC.a` 必须来自同一个 ESP32-P4 包：

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
CONFIG_FREERTOS_TASK_CREATE_ALLOW_EXT_MEM=y
CONFIG_LWIP_MAX_SOCKETS=10
CONFIG_SPIRAM=y
CONFIG_SPIRAM_USE_MALLOC=y
CONFIG_MBEDTLS_TLS_SERVER_AND_CLIENT=y
CONFIG_MBEDTLS_SSL_PROTO_TLS1_2=y
```

建议在应用组件加入 ABI 编译期检查：

```c
#include "freertos/semphr.h"

#if CONFIG_FREERTOS_HZ != 1000
#error "TiRTC ESP32-P4 requires a 1000 Hz FreeRTOS tick"
#endif
#if CONFIG_LWIP_MAX_SOCKETS < 10
#error "TiRTC ESP32-P4 requires at least 10 lwIP sockets"
#endif
#ifdef CONFIG_FREERTOS_USE_TRACE_FACILITY
#error "TiRTC ESP32-P4 requires FreeRTOS trace disabled"
#endif
#ifdef CONFIG_FREERTOS_USE_STATS_FORMATTING_FUNCTIONS
#error "TiRTC ESP32-P4 requires FreeRTOS stats formatting disabled"
#endif
#ifdef CONFIG_FREERTOS_GENERATE_RUN_TIME_STATS
#error "TiRTC ESP32-P4 requires FreeRTOS run-time stats disabled"
#endif
_Static_assert(sizeof(StaticSemaphore_t) == 84,
               "TiRTC ESP32-P4 FreeRTOS ABI mismatch");
```

更换 SDK 或配置后执行 `idf.py fullclean`、`idf.py reconfigure` 和
`idf.py build`。以最终的 `build/config/sdkconfig.h` 与包内
`manifest/build-contract.env` 为准，不要只检查 `sdkconfig.defaults`。

## 启动、内存与媒体任务

- 推荐顺序：PSRAM 初始化成功 → ESP-Hosted/SDIO 初始化 → remote Wi-Fi 启动 → `IP_EVENT_STA_GOT_IP` → 校准系统时间 → `TiRtcInit / TiRtcSetOption / TiRtcStart` → `TIRTC_EVENT_SYS_STARTED`。`TiRtcStart()` 返回 `0` 只表示异步启动请求已受理。
- C6/C61 必须烧录与主机端组件匹配的从机固件。网络事件回调只设置 EventGroup 或投递轻量消息，由应用任务执行时间同步和 TiRTC 启动。
- 使用 C6/C61 Wi-Fi 时，`TIRTC_OPT_NETWORK_TYPE` 仍设置为 `TIRTC_NETCONN_WIFI`；使用 Ethernet 或其他 lwIP netif 时，也必须先建立默认路由和 DNS。
- 大媒体帧、编码输出、接收帧副本、固定帧池和较大后台任务栈适合 PSRAM；SDIO/摄像头/显示 DMA 描述符与 DMA 缓冲、同步对象和小型控制队列应保留在内部 RAM。排查内存时同时观察内部 RAM/PSRAM 的 free、minimum 和 largest block。
- SDK 回调中的帧数据只在回调期间有效，异步处理前必须复制到有上限的应用缓冲区。采集、编解码、硬件重配置和长锁不得放在 SDK 回调线程中。
- 媒体任务使用有界队列；发送返回 `TIRTC_E_BUSY` 时应限流或丢帧，避免积压旧帧。H.264 压缩队列不要任意丢弃中间 P 帧；参考链损坏时清理解码状态并请求新的完整关键帧。

## 包内容

- `include/tirtc/`：TiRTC 公共头文件。
- `lib/libTiRTC.a`：ESP32-P4 RISC-V 静态库，已合入该平台 TGTRP KCP 实现。
- `manifest/build-contract.env`：FreeRTOS/lwIP/ESP-IDF ABI 合同，接入工程必须逐项核对。

## 常见集成问题

### 链接阶段出现 undefined reference

确认使用完整 ESP32-P4 包，组件声明包含示例中的 ESP-IDF 依赖和显式链接组，并清理旧构建缓存；不要混入 S3、P4X、C3 或其他版本的静态库。

### P4 工程能编译但无法联网

依次检查 C6/C61 从机固件与 ESP-Hosted 版本、SDIO 引脚/总线宽度/时钟/复位、remote Wi-Fi 初始化、`IP_EVENT_STA_GOT_IP`、默认路由和 DNS。

### SDK 线程启动即崩溃

核对 `manifest/build-contract.env` 与最终 `build/config/sdkconfig.h`，尤其是 FreeRTOS tick、trace/stats、`StaticSemaphore_t` 和 lwIP socket 数；同时确认 PSRAM 初始化成功。

### HTTPS 请求失败

检查 `CONFIG_MBEDTLS_TLS_SERVER_AND_CLIENT`、`CONFIG_MBEDTLS_SSL_PROTO_TLS1_2`、DNS 和系统时间，不要用改回 HTTP 的方式掩盖 TLS 配置问题。

### 出现 `TIRTC_E_BUSY`、卡顿或 DMA 内存不足

检查回调是否执行耗时操作、媒体队列是否无界增长、大缓冲是否错误占用内部 RAM，以及内部连续 DMA 块是否被 SDIO、摄像头或显示任务耗尽。
