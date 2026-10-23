# 小钛文档中心

这里是供二次开发者使用的项目文档总入口。按开发任务选择入口；当前固件参数
优先看产品媒体合同与对应板卡指南，实验记录只作历史证据。

根目录提供三个稳定入口：[快速体验](../README.md)、[架构与二次开发](../ARCHITECTURE.md)
和[参与开发](../CONTRIBUTING.md)。本页继续收录完整的产品、板卡和工程文档。

## 我想直接使用一块已经适配的开发板

打开[开发板目录](boards/README.md)，按完整型号选择板卡。每块板的使用页都包含规格、
依赖、原理图资料、编译、烧录、首次使用、验证状态和已知限制。

板卡页面提供完整入门步骤；深入修改媒体、驱动或业务时，再按其中链接阅读对应模块。

## 我想了解或体验产品

- [产品文档](product/README.md)：产品定位、功能、交互、媒体能力和用户体验。
- [音视频参数](product/MEDIA_CONTRACT.md)：各功能的流编号、编码、包长、分辨率、帧率和回声处理。
- [根目录 README](../README.md)：下载代码、选择开发板、烧录和首次体验的最短路径。
- [架构与二次开发](../ARCHITECTURE.md)：产品分层、媒体生命周期和扩展路径。
- [常用术语](glossary.md)：查询文档中不可避免的英文缩写和技术名词。

## 我想新增一块开发板

- [参与开发](../CONTRIBUTING.md)：同一芯片下增加板型、接入新平台和准备硬件资料。
- [架构与二次开发](../ARCHITECTURE.md)：目录边界、稳定接口和板卡清单原则。

## 我想维护、测试或发布项目

- [开发板问题与需求清单](boards/ISSUES_AND_REQUIREMENTS.md)：按开发板整理实际问题、功能需求和实板验收缺口。
- [约束规则索引](../CONTRIBUTING.md#约束规则索引)：命名、目录、开发、编译、测试和发布规则的权威出处。
- [固件版本与发布](../CONTRIBUTING.md#固件版本与发布)：产品版本、构建身份、Release 名称及发布出口检查。
- [测试与硬件验证](../tests/README.md)：主机测试、驱动 probe（探针测试）和产品业务 HIL（硬件在环测试）。
- [BK7258 串口命令](development/BK7258_SERIAL_COMMANDS.md)：立创与觅感共用的状态查询、配网、通话和平台地址设置。
- [二次开发易错点](development/pitfalls.md)：界面、音频、BK 重启、异步房间、MQTT 与固件打包的问题证据和回归入口。
- [音视频参数](product/MEDIA_CONTRACT.md)：媒体流编号、设备能力和编解码约定。
- [P4 媒体架构](../firmware/esp-idf/esp32p4/waveshare-touch-lcd-43c/docs/P4_MEDIA_ARCHITECTURE.md)：当前 P4 媒体模块、帧所有权和性能边界。
- [仓库布局](../ARCHITECTURE.md#目录导览)：产品、平台、板卡和固件工程的目录边界。

产品文档统一放在 `docs/product/`；板卡的完整使用文档放在
`docs/boards/<board-id>/`。第三方资料只在相应文档中引用官方链接，不复制到仓库。
开发者自行下载的 SDK、原理图和示例代码可以放在根目录 `.references/`，该目录不提交。

## 当前指南与实验记录

使用和开发按当前指南操作。服务端设计草案与历史实验记录用于理解取舍，不表示功能
已经实现，也不能代替当前板卡参数或发布验证。

- [多人对讲协议](product/ROOM_PROTOCOL.md)：设备 API 路径与服务端设计分别标注。
- [P4 视频性能记录](product/P4_VIDEO_PERFORMANCE.md)：历史 5 fps 基线的测量范围、结果和待验证项。
- [P4 官方资料与实板测量](development/P4_OFFICIAL_MEDIA_RESEARCH.md)：JPEG/PPA 硬件约束与 build.112 阶段计时。
- [P4 两档与 IDF 5.5.5 试验](development/P4_TIERS_IDF_555_TRIAL.md)：12/8 fps 两档、停发保护、固件身份与验证范围。
- [S3 内存优化实验](../firmware/esp-idf/esp32s3/lckfb-esp32s3-xiaotai/docs/ESP32S3_MEMORY_OPTIMIZATION_PLAN.md)：历史方案与已回退路径。

同一参数优先在音视频合同或板卡指南维护，模块文档链接引用。实验记录保留固件身份、
测试条件和限制，不把单次测量写成所有板卡或后续版本的保证。
