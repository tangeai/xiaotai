# 小钛文档中心

[中文](README.md) · [English](README.en.md)

首次使用从[根目录 README](../README.md)开始：了解产品、选择开发板，再下载固件并烧录。
本页汇总对外使用与开发文档。各板参数以对应指南为准，发布版本与下载附件以下载页为准。

## 使用与下载

- [固件下载](downloads.md)：选择板卡、屏幕变体和当前发布固件。
- [开发板目录](boards/README.md)：比较硬件、功能与验证范围。
- [立创·实战派 ESP32-S3](boards/lckfb-esp32s3/README.md)
- [立创·实战派 BK7258](boards/lckfb-bk7258/README.md)
- [微雪 ESP32-P4-WIFI6-Touch-LCD-4.3](boards/waveshare-esp32p4-touch-lcd-43c-v10/README.md)
- [M5Stack CoreS3-Lite](boards/m5stack-cores3-lite/README.md)
- [觅感 AI 玩具开发板 BK7258](boards/mgkfb-bk7258/README.md)
- [常用术语](glossary.md)：查询音视频、硬件与开发术语。

各板指南包含烧录、首次使用、规格、音视频参数、依赖、硬件资料、源码编译、测试及排查。
相同芯片不代表固件可互换，下载前须核对完整型号和硬件版本。

## 开发与维护

- [架构与二次开发](../ARCHITECTURE.md)：产品分层、会话与媒体生命周期、目录边界。
- [参与开发](../CONTRIBUTING.md)：适配流程、测试、文档与发布要求。
- [第三方依赖核对清单](../THIRD_PARTY.md)：依赖来源、授权状态与待办事项。
- [BK7258 串口命令](development/BK7258_SERIAL_COMMANDS.md)：状态查询、配网、通话和服务地址设置。
- [二次开发易错点](development/pitfalls.md)：长期有效的实现约束与回归入口。
- [测试与硬件验证](../tests/README.md)：主机测试、探针测试和硬件在环验证。

## 文档范围

对外文档先检查中文的事实、步骤和表达，再翻译英文；范围以
[翻译清单](../CONTRIBUTING.md#对外文档与翻译范围)为准。
内部产品规范与开发、测试、联调记录不翻译，也不列入公开使用导航。
需要核对实现或验证证据时，由开发指南引用相应资料。

资料保留遵守[内部资料规则](../CONTRIBUTING.md#内部资料与保留方式)：临时记录本地保存，
维护源码和追溯发布所必需的规范、来源及验证证据继续随仓库维护。
