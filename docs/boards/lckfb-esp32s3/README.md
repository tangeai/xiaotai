# 立创·实战派 ESP32-S3

本文中的 H5 指网页端，AI 指人工智能对讲，AEC 指声学回声消除。
Flash 是闪存，PSRAM 是用于音视频缓冲的大块运行内存。

如果只想体验小钛，直接下载发布固件，按“烧录”和“首次使用”操作即可，不需要先搭建
开发环境。

准备修改或编译源码时，需要掌握 C/C++、Git、串口日志和乐鑫 ESP-IDF 的基本
开发流程。第一次接触 ESP32-S3 时，建议先阅读
[ESP32-S3 ESP-IDF 5.5.4 入门指南](https://docs.espressif.com/projects/esp-idf/en/v5.5.4/esp32s3/get-started/)
和[立创开发板官方教程](https://wiki.lckfb.com/zh-hans/szpi-esp32s3/)。

## 烧录

### 直接烧录发布包

从 [GitHub Releases](https://github.com/tangeai/xiaotai/releases) 下载名称以
`lckfb-esp32s3-` 开头的压缩包。解压后先核对 `MANIFEST.json`（发布包清单）中的板卡 ID、
SHA-256 和 `flash_files`，再按包内 `README.txt` 的命令烧录。

也可以使用[乐鑫官方 ESP Web Tool](https://espressif.github.io/esptool-js/)。在 Chrome 或
Edge 中连接本板串口，按 `flash_files` 逐项添加文件和地址后执行 `Program`。该工具不会
自动识别板型或地址，必须烧录完整文件集合；Safari 不支持。

### 从源码编译后烧录

`idf.py` 会调用 ESP-IDF 自带的 `esptool`：

```bash
cd firmware/esp-idf/esp32s3/lckfb-esp32s3-xiaotai
idf.py -p <串口> flash monitor
```

普通升级不擦除整片 Flash。需要清 Wi-Fi 或绑定时使用产品提供的独立入口，避免误删
校准、凭证和量产数据。

## 首次使用

确认发布包中的全部烧录文件都已写入且设备正常重启，再进行下面的配网和绑定。

### 1. 配置 Wi-Fi

首次烧录、执行过 `wifi-clear` 或已保存网络连接失败时，屏幕显示“需要连接网络”，并
广播无密码的 `XiaoTai-XXXX` 热点。用手机连接该热点；提示“无互联网”属于正常现象。
配网页没有自动打开时，访问 `http://192.168.6.1`，选择 2.4 GHz Wi-Fi，输入路由器密码
并提交。热点重新出现表示联网失败，需要检查密码和信号覆盖。
配网页面及文案统一以[产品需求 4.11](../../product/PRODUCT_REQUIREMENTS.md#411-启动无网络与-ap-配网)为准。

### 2. 绑定设备

联网后，屏幕显示并播报 6 位验证码。让手机恢复互联网连接，登录
[小钛设备页](https://xiaotai.chat/devices)，在添加设备处输入屏幕上的验证码。验证码
过期时使用新号码。绑定成功后设备进入首页；串口执行 `status` 应显示平台、MQTT
（设备消息通道）和 TiRTC（实时音视频软件开发工具包）均已就绪。

### 3. 验证与恢复

依次检查屏幕、触摸、按键、摄像头和音频，再验证 H5（网页端）双向对讲、至少三轮 AI
对话、设备呼叫和微信 VoIP（网络语音通话）。每次会话结束后应回到首页，且无异常重启
或持续内存下降。

重新配网执行 `wifi-clear`；只换绑账号执行 `tirtc-clear`。不要擦除整片 Flash。完整
诊断命令见[固件工程说明](../../../firmware/esp-idf/esp32s3/lckfb-esp32s3-xiaotai/README.md)。

## 规格

| 项目 | 内容 |
| --- | --- |
| 板卡 ID（仓库中的开发板唯一名称） | `lckfb-esp32s3` |
| 硬件版本 | PCB V1.0.1 |
| 模组型号 | ESP32-S3-WROOM-1-N16R8 |
| 芯片与平台 | ESP32-S3 / ESP-IDF 5.5.4 |
| 存储 | 16 MB Flash、8 MB PSRAM |
| 显示与触摸 | 320×240 显示，电容触摸 |
| 音频 | ES7210 采集、ES8311 播放、NS4150B 功放，支持 AEC |
| 摄像头 | 实板使用 GC2145，固件输出 MJPEG |
| 网络 | 2.4 GHz Wi-Fi |
| 产品能力 | H5 实时查看/对讲、AI 对讲、设备/微信语音呼叫、屏幕交互 |

开发板对外名称为“立创·实战派 ESP32-S3”，板上使用的乐鑫模组是
`ESP32-S3-WROOM-1-N16R8`。实物硬件以 SZPI ESP32-S3 AIROBOT 和
PCB（印刷电路板）V1.0.1 丝印为准。

## 音视频参数

H5、AI 和语音呼叫使用 8 kHz 单声道 A-law，每包 20 毫秒、160 字节。H5 上行音频为
流 10、下行为流 14；AI 双向音频为流 1。

H5 视频使用流 11，格式为 MJPEG（逐帧 JPEG 图像），分辨率 320×240，目标帧率为
8 fps（每秒 8 帧），单张 JPEG 上限为 128 KiB。

麦克风和扬声器回采先以 16 kHz 进入 AEC（声学回声消除），净化后低通降采样到
8 kHz，再编码成 A-law。完整处理链见[音视频参数与媒体处理](../../product/MEDIA_CONTRACT.md)。

## 环境与依赖

| 依赖项 | 固定版本或文件 | 来源或下载链接 | SHA-256 |
| --- | --- | --- | --- |
| 主机环境 | Linux/WSL2、Git、Python 3 | [Git](https://git-scm.com/downloads)、[Python](https://www.python.org/downloads/) | 无 |
| ESP-IDF | `v5.5.4`，提交 `735507283d5b2f9fb363a1901172dbd9e847945d` | [Espressif 官方仓库](https://github.com/espressif/esp-idf/tree/735507283d5b2f9fb363a1901172dbd9e847945d) | 无 |
| C/C++ 编译器 | `xtensa-esp-elf-gcc 14.2.0`，构建标识 `esp-14.2.0_20260121` | [ESP-IDF 工具清单](https://github.com/espressif/esp-idf/blob/735507283d5b2f9fb363a1901172dbd9e847945d/tools/tools.json) | 无，安装器按工具清单校验下载文件 |
| TiRTC | 2.3.0，`libTiRTC.a` | 无 | `43b06d1da421c7d24cc7fdb1385d600ecdffbfd2d3801f7faf0c540fb5cdbaa2` |
| ESP-IDF 组件 | ESP-SR 2.4.7、LVGL 8.3.11、`esp_codec_dev` 1.6.2、`esp32-camera` 2.1.7、`esp_jpeg` 1.3.1 | [工程依赖锁定文件](../../../firmware/esp-idf/esp32s3/lckfb-esp32s3-xiaotai/dependencies.lock) | 无 |
| 板级组件 | ES7210、ES8311、触摸与屏幕组件 | 无 | 无 |

组件的完整解析版本以工程依赖锁定文件和 TiRTC 组件 manifest（依赖清单）为准。

## 原理图与硬件资料

| 资料名称 | 版本或标识 | 链接 | SHA-256 | 资料状态 |
| --- | --- | --- | --- | --- |
| 立创开发板官方产品资料 | 立创·实战派 ESP32-S3，模组 `ESP32-S3-WROOM-1-N16R8` | [在线文档](https://wiki.lckfb.com/zh-hans/szpi-esp32s3/) | 无 | 已提供 |
| 立创·实战派 ESP32-S3 开发板原理图 | V1.0.1，设计标识 `ESP32-S3-V1_0_1 / XD-ESP32S3-V1_0_1` | [官方开源硬件页面](https://wiki.lckfb.com/zh-hans/szpi-esp32s3/open-source-hardware/) | `5b3718f8caa136597fb9a48e44aa7fd1ebf0001be6dd1506492538fee600fa79` | 已提供 |
| 摄像头教程 | 无 | [在线文档](https://wiki.lckfb.com/zh-hans/szpi-esp32s3/beginner/camera.html) | 无 | 已提供 |
| ES7210 教程 | 无 | [在线文档](https://wiki.lckfb.com/zh-hans/szpi-esp32s3/beginner/audio-input-es7210.html) | 无 | 已提供 |
| ES8311 教程 | 无 | [在线文档](https://wiki.lckfb.com/zh-hans/szpi-esp32s3/beginner/audio-output-es8311.html) | 无 | 已提供 |
| ES7210、ES8311、NS4150B、ZTS6216、PCA9557 数据手册 | 无 | | 无 | 未提供 |

原理图版本和硬件结论的来源记录在工程 Hardware IR（硬件信息记录）中。

## 开发

### 目录结构

从仓库根目录看，这块板相关的代码分布如下：

```text
xiaotai/
├── boards/lckfb/esp32s3/              # 板卡身份、引脚、显示、按键和总线适配
├── product/                                # 跨芯片复用的业务状态、协议与测试
├── platforms/esp-idf/
│   ├── main/app_main.c                     # ESP32-S3 公共启动入口
│   └── components/                         # 公共运行时、页面、联网、语音和 TiRTC 组件
└── firmware/esp-idf/esp32s3/lckfb-esp32s3-xiaotai/
    ├── main/                               # 选择公共入口并配置本工程
    ├── components/starter_media/           # GC2145、音频和 JPEG 媒体适配
    ├── components/starter_voice/           # 唤醒模型和板级语音配置
    ├── components/starter_product/         # 本板使用的页面资源和产品资源
    ├── components/tirtc_sdk/                # TiRTC 库接入与任务策略
    ├── third_party/tirtc/                   # 当前工程使用的 TiRTC 库
    ├── tools/                               # 本工程主机测试入口
    └── *-contract.json                      # 硬件、交互和媒体契约
```

`build/` 是构建生成目录，不是源码入口。依赖组件通过 `idf_component.yml` 和锁定文件
更新，不要直接修改 `managed_components/` 中的下载副本。

### 按任务查找代码

| 要修改的内容 | 首先查看 | 边界说明 |
| --- | --- | --- |
| 板卡型号、引脚、总线、按键和显示初始化 | `boards/lckfb/esp32s3/` | 只放这块板特有的硬件事实 |
| 启动顺序和组件装配 | `platforms/esp-idf/main/app_main.c`、工程 `main/` | 公共入口由板卡清单选择具体适配 |
| H5、AI、设备呼叫和微信 VoIP 的状态与优先级 | `product/src/`、`platforms/esp-idf/components/starter_runtime/` | 产品会话规则必须保持可做主机测试 |
| 页面状态、联系人和交互反馈 | `platforms/esp-idf/components/starter_product/`、`product/interaction/` | 交互层发送意图，不自行维护业务状态 |
| 麦克风、扬声器、AEC、GC2145 和 JPEG | 工程 `components/starter_media/`、`platforms/esp-idf/components/starter_media_common/` | 引脚和外设时序留在板级适配 |
| 离线唤醒和语音模型 | 工程 `components/starter_voice/`、`platforms/esp-idf/components/starter_voice/` | 模型文件和运行逻辑分开维护 |
| 配网、绑定、MQTT、配置和串口命令 | `platforms/esp-idf/components/` 下对应组件 | 板级工程只选择和配置公共组件 |
| TiRTC 连接、流编号和媒体收发 | `platforms/esp-idf/components/starter_tirtc/`、工程 `components/tirtc_sdk/` | 不把 H5、设备呼叫和微信流配置混用 |
| 回归测试和构建约束 | `product/tests/`、工程 `tools/`、`tools/tests/` | 修改行为时先补失败用例，再改实现 |

### 推荐修改顺序

1. 先读根目录 [`AGENTS.md`](../../../AGENTS.md)、[`ARCHITECTURE.md`](../../../ARCHITECTURE.md)
   和[测试说明](../../../tests/README.md)，确认改动属于产品、平台还是板级代码。
2. 修改页面、按键或语音交互前，再读[产品交互规范](../../product/PRODUCT_INTERACTION_PROFILES.md)。
3. 涉及硬件时核对 `hardware-ir.json` 和对应契约；准确器件与引脚不能从相似板卡推断。
4. 先完成公共接口和主机测试，再接入板级实现，最后按本页“测试”章节做实板验证。

## 编译

```bash
bash tools/setup_esp_idf.sh esp32s3
. tools/activate_esp_idf.sh
python3 tools/build.py --board lckfb-esp32s3
```

工程目录为 `firmware/esp-idf/esp32s3/lckfb-esp32s3-xiaotai/`。日常构建使用工程默认 `build/`，
不要复用从其他路径复制来的 CMake cache。

## 测试

```bash
bash tools/check.sh
bash firmware/esp-idf/esp32s3/lckfb-esp32s3-xiaotai/tools/run_host_tests.sh
python3 tools/build.py --board lckfb-esp32s3
```

实板依次验证显示、触摸、GC2145 摄像头、H5、AI、设备呼叫和微信 VoIP。音频测试要
覆盖单讲与双讲，并记录 AEC 输入、播放参考、上行首包和会话结束后的资源释放。

## 排查

- 屏幕或触摸方向错误：先核对 PCB V1.0.1、显示尺寸和触摸坐标变换，不套用其他 S3 板配置。
- H5 或微信单向无声：按发送方向核对流编号、订阅状态、首包计数和 SDK 返回码。
- AI 自己打断自己：确认 AEC 使用实际播放参考，采集和参考均为连续 16 kHz PCM。
- 摄像头异常：确认实板传感器为 GC2145，并检查探测、JPEG 收集和流 11 发送日志。

## 验证状态与已知限制

项目方确认当前固件已在实板跑通。TODO：把结果绑定到精确 BIN/ELF SHA-256，补齐
测试日期、启动与媒体日志摘要、AEC 单讲/双讲指标及板卡照片。

板级配置和 adapter 位于 `boards/lckfb/esp32s3/`；准确硬件事实以 Hardware IR 为准。
