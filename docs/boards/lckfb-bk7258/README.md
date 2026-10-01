# 立创·实战派 BK7258

本文中的 H5 指网页端，AI 指人工智能对讲，AEC 指声学回声消除，
VoIP 指网络语音通话。PSRAM 是用于音视频缓冲的大块运行内存。

如果只想体验小钛，直接下载发布固件，按“烧录”和“首次使用”操作即可，不需要先搭建
开发环境。

准备修改或编译源码时，需要掌握 C 语言、Git、串口日志和 BK7258 的构建流程。
BK7258 采用 AP/CP（应用处理器/协处理器）双镜像架构，不能按单核工程理解。

## 烧录

### 获取固件

从 [GitHub Releases](https://github.com/tangeai/xiaotai/releases) 下载名称以
`lckfb-bk7258-` 开头的压缩包，解压并核对 `MANIFEST.json` 后，使用其中的
`all-app.bin`。自行编译时，使用本页“编译”一节列出的 `all-app.bin` 产物。

### 选择烧录工具

BKFIL 是 Beken 官方烧录工具，下载总入口为
[BKFIL v4 图形工具目录](https://dl.bekencorp.com/tools/bkfil/v4/gui)。该目录按操作系统
分开发布安装包；不要从不明网盘下载改包版本。

| 方式 | 工具入口 | 适用环境 | 说明 |
| --- | --- | --- | --- |
| 网页烧录 | [立创 BK7258 网页烧录工具](https://wiki.lckfb.com/storage/html/bk7258-web-flasher/#/download) | Chrome/Edge | 无需安装客户端，可导入 `all-app.bin` |
| BKFIL Windows 客户端 | [Beken 官方 Windows 目录](https://dl.bekencorp.com/tools/bkfil/v4/gui/windows) | Windows | 下载目录中的 Windows ZIP 包，解压后运行 |
| BKFIL Linux 客户端 | [Beken 官方 Linux 目录](https://dl.bekencorp.com/tools/bkfil/v4/gui/linux) | Linux | 下载目录中的 Linux ZIP 包，适合开发与批量验证 |
| BKFIL macOS 客户端 | [Beken 官方 macOS 目录](https://dl.bekencorp.com/tools/bkfil/v4/gui/macos) | macOS | 下载目录中的 DMG 安装包 |

BKFIL 的界面与参数说明见[Beken BKFIL v4 官方文档](https://docs.bekencorp.com/arminodoc/bk_app/bkfil_v4/zh_CN/latest/index.html)，
开发板接线和进入下载模式的方法见
[立创官方固件烧录教程](https://wiki.lckfb.com/zh-hans/szpi-bk7258/szpi-bk7258/basic/burn.html)。

从 `0x00000000` 完整烧录 `all-app.bin`。`app_pack.rbl` 只用于 OTA（无线升级），不能代替
首次整包烧录。烧录工具配置必须保留 RF（射频）校准、工厂数据和凭证分区；不确定分区时
先读取并备份，不执行全片擦除。

## 首次使用

确认 `all-app.bin` 已经完整烧录且设备正常重启，再进行下面的配网和绑定。

### 1. 配置 Wi-Fi

首次烧录、执行过 `wifi-clear` 或已保存网络连接失败时，屏幕显示配网提示，并广播
无密码的 `XiaoTai-XXXX` 热点。用手机或电脑连接该热点；提示“无互联网”属于正常现象。
配网页没有自动打开时，访问 `http://192.168.6.1`，选择 2.4 GHz Wi-Fi，输入路由器密码
并提交。热点重新出现表示联网失败，需要检查密码和信号覆盖。

### 2. 绑定设备

联网后，屏幕显示 6 位验证码，扬声器也会播报。让手机或电脑恢复互联网连接，登录
[小钛设备页](https://xiaotai.chat/devices)，在添加设备处输入屏幕上的验证码。验证码
过期时使用屏幕刷新后的号码。屏幕显示“准备就绪”且串口进入 `READY`，表示绑定成功。

### 3. 验证与恢复

依次检查屏幕、触摸、按键、摄像头、麦克风和扬声器，再验证 H5（网页端）双向对讲、
至少三轮 AI 对话以及微信 VoIP（网络语音通话）双向语音。每次会话结束后应回到
`READY`，且没有 UsageFault（处理器用法异常）、异常重启或持续内存下降。

重新配网执行 `wifi-clear`；只换绑账号执行 `tirtc-clear`。不要使用全片擦除。完整诊断
命令见[固件工程说明](../../../firmware/beken/bk7258/lckfb-bk7258-xiaotai/README.md)。

## 规格

| 项目 | 内容 |
| --- | --- |
| 板卡 ID（仓库中的开发板唯一名称） | `lckfb-bk7258` |
| 芯片与平台 | BK7258 / BK-AVDK SMP 3.1.1.8 |
| 存储与内存 | 8 MiB Flash、16 MiB PSRAM、640 KiB SRAM（均为当前分区/内存配置声明） |
| 显示与触摸 | ST7789V2 SPI 屏、FT6336 触摸 |
| 音频 | 板载 ADC/DAC、麦克风、扬声器和功放；AI 支持全双工 AEC，8 kHz 通话当前使用 AGC |
| 摄像头 | GC0308 DVP，H.264 640×480@20 目标档位 |
| 网络 | 2.4 GHz Wi-Fi |
| 产品能力 | H5 实时查看/双向对讲、AI 对讲、设备呼叫、微信 VoIP |

板卡照片和 PCB（印刷电路板）版本仍需归档。当前身份要结合实物丝印与启动探针报告核对。
上述 Flash、PSRAM 和 SRAM 容量来自当前 BK7258 工程的分区与内存区域配置；其中 AP
启动日志显示约 10.6 MiB 的动态 PSRAM 堆，这是分配给 AP 的可用区域，不是物理 PSRAM
总容量。当前 AP 探针不能读取 Flash JEDEC ID，因此 8 MiB Flash 尚未获得运行时器件识别
结果，不能表述为实测容量。

## 音视频参数

H5、设备呼叫和微信通话使用 8 kHz 单声道 A-law，每个网络包 40 毫秒、320 字节。
AI 使用 16 kHz 单声道 Opus，每包 20 毫秒，目标码率 16 kbit/s；编码包长度可变，
当前单包缓冲上限为 512 字节。

视频上行使用 H.264，目标为 640×480、20 fps（每秒 20 帧）。设备会记录实际帧率、码率、
关键帧和丢帧数，不能只用配置值判断链路质量。

AI 的 AEC（声学回声消除）按 20 毫秒、320 个采样处理，播放参考来自实际送入 DAC
（数模转换器）的 PCM 音频，随后依次执行 NS 和 AGC。8 kHz H5、设备呼叫、微信 VoIP
及多人对讲每个处理块为 160 个采样，当前只启用 AGC；AEC/NS 在完成实板验证前保持关闭。

完整流编号和 AEC 边界见[音视频参数与媒体处理](../../product/MEDIA_CONTRACT.md)。

## 环境与依赖

| 依赖项 | 固定版本或文件 | 来源或下载链接 | SHA-256 |
| --- | --- | --- | --- |
| 主机环境 | Linux/WSL2、Git、Python 3 | [Git](https://git-scm.com/downloads)、[Python](https://www.python.org/downloads/) | 无 |
| BK-AVDK SMP | `release/v3.1.1.8`，提交 `1cfd56af09a3cb6470f35f1e0c604035ed1b6ee7` | [Beken 官方仓库](https://gitee.com/bekencorp/bk_avdk_smp/tree/1cfd56af09a3cb6470f35f1e0c604035ed1b6ee7) | 无 |
| C/C++ 编译器 | `arm-none-eabi-gcc 10.3.1 20210824`，`gcc-arm-none-eabi-10.3-2021.10-x86_64-linux.tar.bz2` | [Beken 官方 Arm 工具链目录](https://dl.bekencorp.com/tools/toolchain/arm) | `97dbb4f019ad1650b732faffcc881689cedc14e2b7ee863d390e0a41ef16c9a3` |
| TiRTC-Nano | `tirtc__beken-bk7258__gcc-arm-none-eabi-10.3-2021.10__v2.5.0__mini.tgz`，工程使用其中的 `libTiRTC.a` | [BK7258 发布目录](https://repo-sdk.tange-ai.com/service/rest/repository/browse/tirtc-sdks/releases/beken-bk7258/)、[v2.5.0 下载包](https://repo-sdk.tange-ai.com/repository/tirtc-sdks/releases/beken-bk7258/tirtc__beken-bk7258__gcc-arm-none-eabi-10.3-2021.10__v2.5.0__mini/tirtc__beken-bk7258__gcc-arm-none-eabi-10.3-2021.10__v2.5.0__mini.tgz) | 下载包：`348c6571ac3aff61c0fc90ab2fde40588f155a63514063ded6956feae3f84cf7`；工程内静态库：`47e26827ca2419084163e3656dca4d6e508e433381784e9c03f232d3dbaa4171` |
| 厂商组件 | AP/CP 多媒体、网络、显示、触摸、DVP（数字视频端口）和音频组件 | [BK-AVDK SMP 源码](https://gitee.com/bekencorp/bk_avdk_smp/tree/1cfd56af09a3cb6470f35f1e0c604035ed1b6ee7) | 无 |

Beken 工程保持独立的 AP/CP（应用处理器/协处理器）、分区、TLS ABI（线程局部存储应用二进制接口）和 TiRTC 静态库，不能套用 ESP-IDF 依赖。

第一次开发 BK7258 时，先按[立创官方环境搭建](https://wiki.lckfb.com/zh-hans/szpi-bk7258/szpi-bk7258/basic/programming-environment-setup.html)
准备主机，再阅读 [AP/CP 双镜像架构](https://wiki.lckfb.com/zh-hans/szpi-bk7258/szpi-bk7258/advanced-guide/ap-cp-architecture.html)。

需要查阅编译选项和平台接口时，使用 [Beken BK7258 SDK 文档](https://docs.bekencorp.com/arminodoc/bk_avdk_smp/smp_doc/bk7258/en/v3.1.1/index.html)。

## 原理图与硬件资料

| 资料名称 | 版本或标识 | 链接 | SHA-256 | 资料状态 |
| --- | --- | --- | --- | --- |
| `ATK-DNT5M_V1.0+原理图.PDF` | 图纸标识 `ATK-DNT5M V1.0+` | 无 | `faf535aea976d599d1576f21ff07d38600486b01fc18925c8bdc7f024c5203ef` | 未提供 |
| BK7258 SMP 官方文档 | v3.1.1 | [在线文档](https://docs.bekencorp.com/arminodoc/bk_avdk_smp/smp_doc/bk7258/en/v3.1.1/index.html) | 无 | 已提供 |
| BK7258 Datasheet | 无 | [PDF](https://docs.bekencorp.com/spec/BK7258/BK7258%C2%A0Datasheet.pdf) | 无 | 已提供 |
| BK7258 官方硬件资料索引 | v3.1.1 | [在线文档](https://docs.bekencorp.com/arminodoc/bk_ai_smp/bk7258/en/v3.1.1/hw-reference/index.html) | 无 | 已提供 |
| 立创·实战派 BK7258 官方板卡页 | 无 | 无 | 无 | 未提供 |

引脚和外设结论以工程 Hardware IR（硬件信息记录）及实板验证为准。

## 开发

### 目录结构

```text
xiaotai/
├── boards/lckfb/bk7258/                板级引脚和外设适配
├── product/                                  跨芯片复用的产品状态与协议
├── platforms/common/                         公共板级 C 接口
└── firmware/beken/bk7258/lckfb-bk7258-xiaotai/
    ├── ap/                                   AP 应用核，小钛主要功能代码
    │   ├── ap_main.c                         AP 启动入口
    │   ├── include/                          AP 模块公开头文件
    │   ├── src/                              业务协调、网络、媒体和界面适配
    │   ├── assets/                           界面字体、图像和二维码资源
    │   └── config/                           AP 工程配置
    ├── cp/                                   CP 通信核及 AP 拉起入口
    ├── partitions/                           BK7258 分区配置
    ├── third_party/tirtc/                    固定版本 TiRTC 头文件和静态库
    ├── tools/                                环境、构建、诊断和主机测试脚本
    ├── CMakeLists.txt、Makefile、pj_config.mk 工程构建入口
    └── probe-report.*.json                   硬件探测与构建前检查结果
```

`build/`、`output/` 和 `tools/__pycache__/` 是生成目录，不是源码入口。不要直接修改其中
的文件，也不要让源码引用这些目录。

### 按任务查找代码

| 要修改的内容 | 首先查看 | 说明 |
| --- | --- | --- |
| 开发板型号、构建入口和能力声明 | [`boards/lckfb/bk7258/board.json`](../../../boards/lckfb/bk7258/board.json) | 只记录构建元数据，不作为硬件接线证据 |
| GPIO、电源、按键、触摸、显示、摄像头和音频接线 | [`boards/lckfb/bk7258/`](../../../boards/lckfb/bk7258/) | `board_config.h` 保存板级常量，`board_*.c` 实现具体外设适配 |
| AP/CP 启动顺序或组件装配 | [`ap/ap_main.c`](../../../firmware/beken/bk7258/lckfb-bk7258-xiaotai/ap/ap_main.c)、[`cp/cp_main.c`](../../../firmware/beken/bk7258/lckfb-bk7258-xiaotai/cp/cp_main.c)、[`ap/CMakeLists.txt`](../../../firmware/beken/bk7258/lckfb-bk7258-xiaotai/ap/CMakeLists.txt) | CP 负责平台启动，主要产品功能运行在 AP |
| H5、AI、设备呼叫、微信 VoIP 和多人对讲的优先级 | [`product/src/xiaotai_runtime.c`](../../../product/src/xiaotai_runtime.c)、[`ap/src/xiaotai_app.c`](../../../firmware/beken/bk7258/lckfb-bk7258-xiaotai/ap/src/xiaotai_app.c) | 前者维护跨芯片会话规则，后者串行执行 BK7258 上的状态切换 |
| 联系人、呼叫协议、多人房间和 AI 协议 | [`product/src/`](../../../product/src/) | 先改公共模块及对应 `product/tests/`，不要在 BK 适配层复制一套协议 |
| 配网热点和 `192.168.6.1` 页面 | [`xiaotai_network.c`](../../../firmware/beken/bk7258/lckfb-bk7258-xiaotai/ap/src/xiaotai_network.c) | 管理 STA（连接路由器的工作模式）、SoftAP（设备配网热点）和配网页 |
| 设备绑定、服务发现、HTTP 和 MQTT | [`xiaotai_platform_client.c`](../../../firmware/beken/bk7258/lckfb-bk7258-xiaotai/ap/src/xiaotai_platform_client.c) | 负责平台接口和消息通道，不负责媒体资源所有权 |
| TiRTC 连接、流编号、回调和异常恢复 | [`xiaotai_tirtc.c`](../../../firmware/beken/bk7258/lckfb-bk7258-xiaotai/ap/src/xiaotai_tirtc.c)、[`xiaotai_tirtc_recovery.c`](../../../firmware/beken/bk7258/lckfb-bk7258-xiaotai/ap/src/xiaotai_tirtc_recovery.c) | SDK 回调只投递有界事件，不能直接修改产品状态 |
| 麦克风、扬声器、编解码和 AEC | [`xiaotai_audio.c`](../../../firmware/beken/bk7258/lckfb-bk7258-xiaotai/ap/src/xiaotai_audio.c)、[`board_audio.c`](../../../boards/lckfb/bk7258/board_audio.c) | 通用媒体流程放 AP 适配层，ADC、DAC、DMA 和功放控制放板级代码 |
| 摄像头采集、H.264 和视频发送 | [`xiaotai_video.c`](../../../firmware/beken/bk7258/lckfb-bk7258-xiaotai/ap/src/xiaotai_video.c)、[`board_camera.c`](../../../boards/lckfb/bk7258/board_camera.c) | 业务节流规则复用 `product/src/xiaotai_video_pacer.c` |
| 屏幕页面、触摸区域和按键意图 | [`xiaotai_ui.c`](../../../firmware/beken/bk7258/lckfb-bk7258-xiaotai/ap/src/xiaotai_ui.c)、[`xiaotai_touch.c`](../../../firmware/beken/bk7258/lckfb-bk7258-xiaotai/ap/src/xiaotai_touch.c)、[`xiaotai_button.c`](../../../firmware/beken/bk7258/lckfb-bk7258-xiaotai/ap/src/xiaotai_button.c) | 页面渲染留在适配层，业务状态仍由产品运行时管理 |
| 持久化、串口命令、运行指标和硬件探针 | [`ap/src/`](../../../firmware/beken/bk7258/lckfb-bk7258-xiaotai/ap/src/) | 分别从 `xiaotai_storage.c`、`xiaotai_console.c`、`xiaotai_metrics.c` 和 `xiaotai_probe.c` 开始 |
| 分区、编译环境和打包 | [`partitions/`](../../../firmware/beken/bk7258/lckfb-bk7258-xiaotai/partitions/)、[`tools/`](../../../firmware/beken/bk7258/lckfb-bk7258-xiaotai/tools/) | 修改后同时验证 AP、CP、整包和 Flash 布局 |

### 推荐修改顺序

1. 先判断改动属于产品规则、BK 平台适配还是当前开发板硬件，避免在错误层级修补。
2. 修改业务行为前，从 `product/tests/` 或 AP 公共接口增加会失败的回归用例，再改实现。
3. 修改引脚、总线、DMA、时钟、电源或器件初始化时，只改板级/平台代码，并核对
   `probe-report.hardware.json` 和实板测量结果。
4. 修改界面、按钮或用户流程前，阅读
   [`PRODUCT_INTERACTION_PROFILES.md`](../../product/PRODUCT_INTERACTION_PROFILES.md)；修改房间
   分配触发条件时，以其中的“房间分配刷新”一节为准。
5. 完成后运行本页“测试”一节中的检查和构建命令；涉及外设或媒体时，还要执行对应实板
   验证，不能只看编译结果。

工程内 [`AGENTS.md`](../../../firmware/beken/bk7258/lckfb-bk7258-xiaotai/AGENTS.md) 记录
BK7258 的额外修改约束；根目录 [`ARCHITECTURE.md`](../../../ARCHITECTURE.md) 说明整体
分层，[测试说明](../../../tests/README.md)说明测试要求。

## 编译

```bash
bash firmware/beken/bk7258/lckfb-bk7258-xiaotai/tools/setup_build_env.sh
python3 tools/build.py --board lckfb-bk7258
```

整包产物位于
`firmware/beken/bk7258/lckfb-bk7258-xiaotai/build/bk7258/lckfb-bk7258-xiaotai/package/all-app.bin`。

## 测试

```bash
bash tools/check.sh
python3 tools/build.py --board lckfb-bk7258
```

实板验证要覆盖显示、触摸、GC0308 摄像头、8 kHz A-law/AGC、16 kHz Opus/AEC/NS/AGC、H5、AI
和微信 VoIP。结束会话后确认状态回到 `READY`，音频上下行计数正确，任务数和内存水位
恢复且设备没有重启。

## 排查

- H5 下行无声：检查流 14 是否收到数据、A-law 解码是否产出 PCM、DAC 写入和功放 GPIO。
- 微信上行无声：检查流 0 的平台接受状态；持续 `rc=-40006` 表示上行尚未就绪。
- 视频连接中断：记录 TiRTC 缓冲占用、实际码率、关键帧和丢帧，不只看 Wi-Fi RSSI。
- AI 异常或自打断：核对 Opus 包长、AEC 播放参考、`ref-under` 统计和会话结束顺序。

## 验证状态与已知限制

项目方确认 H5 实时查看/双向对讲、AI 对讲和微信 VoIP 已在实板跑通。TODO：把结果
绑定到精确 `all-app.bin` SHA-256，并补齐测试日期、长稳时长、Flash JEDEC 识别、PCB revision
和板卡照片。

含凭证的原始日志和按日期记录的稳定性调查不进入源码仓库；当前验证结论应写入与
固件 SHA-256 绑定的 HIL 记录。
