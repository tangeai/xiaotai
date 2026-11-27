# 第三方资产清单

[中文](THIRD_PARTY.md) · [English](THIRD_PARTY.en.md)

本文件记录随仓库和固件分发的二进制来源。根目录 MIT License 覆盖项目自有源码和文档。

维护者确认 TiRTC SDK、唤醒模型 `nihaoxiaotai`、提示音和铃声为项目自有，允许再分发。下表各项都不构成公开发布阻塞。

| 资产 | 分发 | 来源 |
| --- | --- | --- |
| `platforms/**/third_party/tirtc/` 和 `firmware/**/components/tirtc_sdk/` 内的 TiRTC 静态库及头文件 | 自有，允许再分发 | 各 SDK 包的 README/manifest；ESP32-S3、ESP32-P4 和 BK7258 包均为 2.6.3 |
| `platforms/esp-idf/components/starter_voice/vendor/` | 随仓库分发 | Voicute onnx-wakeword 的 ESP32 前处理/后处理子集，提交见下文 |
| `product/assets/models/nihaoxiaotai/` 及固件工程中的目标模型副本 | 自有，允许再分发 | 项目唤醒模型及配套 head 数据 |
| ESP32 共用 `ui_font_cn_16.c`、`ui_font_cn_24.c` 及 P4 `main/ui/font/lv_font_cn_14.c` | SIL OFL 1.1 | Noto Sans CJK SC Regular 2.004，固定源码与哈希见[字体来源](product/assets/fonts/noto-sans-sc/README.md)。分发时保留该目录的 OFL 与版权声明 |
| `product/assets/audio/` 及固件工程中保留的提示音、铃声副本 | 自有，允许再分发 | 工程内嵌提示音和铃声 |
| ESP32-P4 工程中的预编译 H.264 库及配套组件 | 按组件许可证分发 | 板卡工程携带的二进制和头文件，组件目录含 Apache-2.0 LICENSE |
| IDF managed components | 遵循各组件许可证 | `dependencies.lock` 锁定的供应商组件。保留各组件 LICENSE 和 NOTICE |

Voicute 本地参考源码提交：`61e76c5a12ac33da0def7bceba2e07228ad03e63`。
