<!-- tirtc-package-readme: static-v1 -->
# TiRTC Nano SDK：BK7258 Mini

本包为官方 Beken BK AVDK SMP `release/v3.1.1.10` 提供 TiRTC 头文件和预编译静态库
`lib/libTiRTC.a`，用于在 BK7258 固件中接入 TiRTC 设备通信能力。

## 使用前确认

- 平台与版本：官方 Beken BK AVDK SMP `release/v3.1.1.10`。
- 目标与 ABI：Arm hard-float；应用工程的 CPU、浮点 ABI 和 libc 配置必须与
  `manifest/build-info.env` 一致。
- `TiRtcConnect(remote_id, ...)` 中的 `remote_id` 表示要连接的目标设备；
  `TiRtcWhipConnect(service_desc, ...)` 中的 `service_desc` 表示要连接的 WHIP
  服务。本包使用 TiRTC/TGTRP 的 KCP 通信方式，因此目标设备或目标 WHIP 服务
  必须支持同一方式；不能直接连接浏览器或只支持标准 WebRTC（DTLS/SCTP）的
  第三方实现。

本包不支持 HTTPS。如果应用通过 `TIRTC_OPT_SERVICE_ENDPOINT` 配置 TiRTC 服务
入口，必须在 `TiRtcStart()` 前传入 `http://` 地址：

```c
static const char endpoint[] = "http://example.invalid";
TiRtcSetOption(TIRTC_OPT_SERVICE_ENDPOINT, endpoint, sizeof(endpoint) - 1);
```

传入 `https://` 地址会返回 `TIRTC_E_INVALID_PARAMETER`。调用
`TiRtcWhipConnect(...)` 后，如果平台服务为本次连接返回 WHIP 地址，该地址也必须
能通过 HTTP 访问。

## 工程集成

将解压后的 `include/` 和 `lib/` 放入同一个 Armino 组件目录，例如：

```text
components/tirtc_sdk/
├── CMakeLists.txt
├── app_tirtc.c
├── include/tirtc/
└── lib/libTiRTC.a
```

在该组件的 `CMakeLists.txt` 中完成组件声明、预编译库注册和最终链接：

```cmake
armino_component_register(
    SRCS "app_tirtc.c"
    INCLUDE_DIRS "include"
    PRIV_REQUIRES bk_common lwip_intf_v2_1 bk_vfs bk_rtos psa_mbedtls zlib
)

add_prebuilt_library(tirtc_sdk "${CMAKE_CURRENT_LIST_DIR}/lib/libTiRTC.a")
target_link_libraries(
    ${COMPONENT_LIB}
    INTERFACE
        "-Wl,--whole-archive"
        tirtc_sdk
        "-Wl,--no-whole-archive"
)
```

`psa_mbedtls` 由上述官方 AVDK 提供，本包没有重复交付。`libTiRTC.a` 使用其
mbedTLS 3.5.2 头文件和实现满足 AES、DES、SHA、MD5 等密码算法依赖；这项依赖
不表示本包支持 HTTPS 或 DTLS。请保留 `PRIV_REQUIRES` 中的 `psa_mbedtls`，并按
示例对 `libTiRTC.a` 使用 whole-archive。不要混用旧版 mbedTLS 2.25.0 兼容头或库。

## 包内容

- `include/tirtc/`：TiRTC 公共头文件
- `lib/libTiRTC.a`：BK7258 Arm hard-float 静态库
- `manifest/build-info.env`：目标架构、ABI、工具链、平台 SDK 和功能开关

## 常见集成问题

### 链接时报 mbedTLS 未解析符号

确认当前组件的 `PRIV_REQUIRES` 中包含 `psa_mbedtls`，且使用的是官方 AVDK
`release/v3.1.1.10` 所带的 mbedTLS 3.5.2 组件；仅链接 `libTiRTC.a` 不足以提供
这些密码算法符号。

### `TIRTC_OPT_SERVICE_ENDPOINT` 返回 `TIRTC_E_INVALID_PARAMETER`

检查传入的 TiRTC 服务入口。当前包只接受 `http://` 地址，不接受 `https://`。

### 已创建连接，但目标设备或 WHIP 服务无法完成协商

确认 `TiRtcConnect(remote_id, ...)` 指定的目标设备，或
`TiRtcWhipConnect(service_desc, ...)` 指定的 WHIP 服务支持 TiRTC/TGTRP KCP
通信。浏览器和只支持标准 WebRTC DTLS/SCTP 协商的第三方实现不兼容本包。
