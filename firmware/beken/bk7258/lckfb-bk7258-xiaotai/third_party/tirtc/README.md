<!-- tirtc-package-readme: static-v1 -->
# TiRTC Nano SDK：BK7258 Mini

## 包类型与能力

本包适用于官方 Beken BK AVDK SMP `release/v3.1.1.8`，目标为 Arm hard-float ABI。

- 传输模式：KCP-only
- KCP：支持
- HTTPS：不支持
- SCTP：不支持
- DTLS：不支持
- 密码学实现：官方 AVDK `psa_mbedtls` 3.5.2
- mbedTLS 静态库：不随包交付

当前包的完整构建事实见 `manifest/build-info.env`。

## 包内容

- `include/tirtc/`：TiRTC 公共头文件
- `lib/libTiRTC.a`：最终 Arm 静态库
- `manifest/build-info.env`：目标架构、ABI、工具链、平台 SDK 和功能开关

## 接入方法

将解压后的 `include/` 和 `lib/` 放入同一个 Armino 组件目录，例如：

```text
components/tirtc_sdk/
├── CMakeLists.txt
├── app_tirtc.c
├── include/tirtc/
└── lib/libTiRTC.a
```

在该组件的 `CMakeLists.txt` 中注册头文件、平台组件和预编译库：

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

`psa_mbedtls` 必须保留在 `PRIV_REQUIRES` 中，`libTiRTC.a` 必须按上例使用 whole-archive 链接。只添加 `libTiRTC.a`、但不声明 `psa_mbedtls` 组件依赖，会产生 mbedTLS 未解析符号。

## 兼容要求

- 使用官方 AVDK `psa_mbedtls` 3.5.2 提供的头文件、配置和实现。
- 不要混用旧版 mbedTLS 2.25.0 兼容头。
- 本包不支持 HTTPS 或 DTLS；`PRIV_REQUIRES` 中的官方 `psa_mbedtls`
  组件用于满足 AES、DES、SHA、MD5 等密码学接口依赖，不需要额外引入
  TLS/DTLS 库。
- 应用工程的 CPU 架构、hard-float ABI 和 libc 配置必须与 `manifest/build-info.env` 兼容。
