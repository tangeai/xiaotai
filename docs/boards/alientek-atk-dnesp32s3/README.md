# ALIENTEK ATK-DNESP32S3 V1.4

[返回开发板目录](../README.md) · [下载固件及发布说明](https://github.com/tangeai/xiaotai/releases/tag/alientek-atk-dnesp32s3-v1.0.0)

板卡 ID：`alientek-atk-dnesp32s3`。当前正式固件为 **v1.0.0 · build.1**，
用户已确认实板可用。先核对 V1.4 丝印和摄像头模组，再下载本板固件。

本文中的 H5 指网页端，AI 指人工智能对讲，SDK 指软件开发工具包。
Flash 是闪存，PSRAM 是用于音视频缓冲的大块运行内存。

## 烧录

### 打开烧录工具

使用电脑上的 **Chrome 或 Edge** 打开[乐鑫 ESP Web Tool](https://espressif.github.io/esptool-js/)；
网页烧录无需安装 ESP-IDF。关闭占用设备串口的终端或其他烧录网页。
按[正点原子官方板卡资料](https://github.com/openedv/ATK-DNESP32S3-Board/tree/c7434a3da5b9e6feda05added5d6a686f1c95f13)
确认 USB 串口和 BOOT/RESET 操作。

### 烧录步骤

1. 下载 [alientek-atk-dnesp32s3-1.0.0+build.1-merged.bin](https://github.com/tangeai/xiaotai/releases/download/alientek-atk-dnesp32s3-v1.0.0/alientek-atk-dnesp32s3-1.0.0%2Bbuild.1-merged.bin)。首次烧录只选择这一个完整 BIN，地址为 **`0x0`**。
2. 用本板串口连接电脑；如未识别下载模式，按板卡 BOOT/RESET 操作进入下载模式。连接目标必须显示 **ESP32-S3**。
3. 在网页工具中点击 **Connect**，选择本板串口；点击 **Choose File** 选择完整 BIN，**Flash Address 填 `0x0`**，Flash Mode、Frequency、Size 选 `keep`。
4. 点击 **Program**，等待 `Hash of data verified.` 和 `Leaving...`，且没有 `Error:`；断开连接并复位。
5. 用 115200 波特率日志核对项目 `atk_xiaotai_s3` 和版本 `1.0.0+build.1`，再进入“首次使用”。

**完整镜像会覆盖 NVS 中的 Wi-Fi、绑定等持久配置。** 烧录后需重新配网绑定；
`keep` 只保留镜像中的 Flash 参数，不表示保留设备配置。

完整 BIN 的 SHA-256 为
`6e98304bfce9347c7205bbd47ba444cd12b0ac5c67c7a8c7540ddeae102b6276`。
发布页的 `SHA256SUMS` 同时覆盖完整 BIN 和分文件包。

### 保留配置的分文件烧录

下载并解压 [alientek-atk-dnesp32s3-1.0.0+build.1-validation.zip](https://github.com/tangeai/xiaotai/releases/download/alientek-atk-dnesp32s3-v1.0.0/alientek-atk-dnesp32s3-1.0.0%2Bbuild.1-validation.zip)，
按包内 `MANIFEST.json` 和 `flasher_args.json` 核对文件、地址及分区兼容性：

| 文件 | 烧录地址 | 用途 |
|---|---|---|
| `bootloader/bootloader.bin` | `0x0` | 启动引导程序 |
| `partition_table/partition-table.bin` | `0x8000` | 分区表 |
| `atk_xiaotai_s3.bin` | `0x10000` | 应用程序，不能作为完整镜像写入 `0x0` |

在网页工具用 **Add File** 分别添加上述 BIN，填写各自地址，再点击 **Program**。
保留配置升级不做全片擦除，且应先确认分区布局和配置兼容。

### 从源码编译并烧录

需要 ESP-IDF 5.5.5。在仓库根目录执行：

```bash
. tools/activate_esp_idf.sh
python3 tools/build.py --board alientek-atk-dnesp32s3
cd firmware/esp-idf/esp32s3/atk-dnesp32s3
idf.py -p <串口> flash monitor
```

`<串口>` 换成本机实际端口；普通升级不要执行 `erase-flash`。

## 首次使用

确认固件已经完整烧录且设备正常重启，再进行下面的配网和绑定。

### 1. 配置 Wi-Fi

首次烧录、执行过 `wifi-clear` 或已保存网络连接失败时，设备会广播无密码的
`XiaoTai-XXXX` 热点。用手机或电脑连接该热点；提示“无互联网”属于正常现象。配网页
没有自动打开时，访问 `http://192.168.6.1`，选择 2.4 GHz Wi-Fi，输入路由器密码并提交。
热点随后消失表示设备正在联网；热点重新出现时，检查密码和信号覆盖。
配网页面及文案统一以[产品需求 4.11](../../product/PRODUCT_REQUIREMENTS.md#411-启动无网络与-ap-配网)为准。

### 2. 绑定设备

联网后，设备通过扬声器播报 6 位验证码。让手机或电脑恢复互联网连接，登录
[小钛设备页](https://xiaotai.chat/devices)，在添加设备处输入验证码。验证码过期时等待
设备播报新号码。绑定完成并且串口进入 `READY` 后，设备才算准备就绪。

### 3. 验证与恢复

依次检查麦克风和扬声器、H5（网页端）画面、H5 双向语音，再完成至少三轮 AI 对话。
每次会话结束后应回到 `READY`，且无异常重启或持续内存下降。

重新配网执行 `wifi-clear`；只换绑账号执行 `tirtc-clear`。不要用整片擦除代替这两个
命令。完整串口说明见[固件工程说明](../../../firmware/esp-idf/esp32s3/atk-dnesp32s3/README.md)。

## 规格

| 项目 | 内容 |
| --- | --- |
| 板卡 ID（仓库中的开发板唯一名称） | `alientek-atk-dnesp32s3` |
| 芯片与平台 | ESP32-S3 / ESP-IDF 5.5.5 |
| 存储 | 16 MB Flash、8 MB PSRAM |
| 音频 | ES8388，板载麦克风/扬声器链路 |
| 摄像头 | ATK-MC2640 或 ATK-MC5640 模组，固件输出 MJPEG |
| 交互 | 按键，无产品屏幕 |
| 网络 | 2.4 GHz Wi-Fi |
| 产品能力 | H5 实时画面与双向语音、AI 对讲、设备配网与绑定 |

实物以 ATK-DNESP32S3 和 PCB（印刷电路板）V1.4 丝印为准。摄像头模组也要单独核对，不能只看
主板型号。

## 音视频参数

当前正式版 **v1.0.0 · build.1** 的参数如下。音频均为单声道，每包 20 毫秒。

| 场景 | 方向与流编号 | 编码与采样率 | 包大小或画面参数 |
|---|---|---|---|
| AI 对讲 | 上行、下行：流 1 | 上行 A-law / 16 kHz；下行 A-law / 8 kHz | 上行 320 字节；下行 160 字节 |
| H5 实时对讲 | 上行：流 10；下行：流 14 | 双向 A-law / 8 kHz | 每包 160 字节 |
| H5 实时画面 | 上行：流 11 | MJPEG（逐帧 JPEG） | 320×240，最高 5 fps，单帧上限 128 KiB |

当前能力表没有声明 AEC（声学回声消除）。

各功能的流编号和包长解释见[音视频参数与媒体处理](../../product/MEDIA_CONTRACT.md)。

## 环境与依赖

| 依赖项 | 固定版本或文件 | 来源或下载链接 | SHA-256 |
| --- | --- | --- | --- |
| 主机环境 | Linux/WSL2、Git、Python 3 | [Git](https://git-scm.com/downloads)、[Python](https://www.python.org/downloads/) | 无 |
| ESP-IDF | `v5.5.5`，提交 `b774170ff46c393eeb5e495ea37936038d3f4f4f` | [Espressif 官方仓库](https://github.com/espressif/esp-idf/tree/b774170ff46c393eeb5e495ea37936038d3f4f4f) | 无 |
| C/C++ 编译器 | `xtensa-esp-elf-gcc 14.2.0`，构建标识 `esp-14.2.0_20260121` | [ESP-IDF 工具清单](https://github.com/espressif/esp-idf/blob/b774170ff46c393eeb5e495ea37936038d3f4f4f/tools/tools.json) | 无，安装器按工具清单校验下载文件 |
| TiRTC | 2.5.0，`libTiRTC.a` | 无 | `7334e846ed4261b5297607c85acafe1d586a22374f7c8e37b102b2742e47c7aa` |
| ESP-IDF 组件 | `esp32-camera` 2.1.7、`esp_jpeg` 1.3.1 | [工程依赖锁定文件](../../../firmware/esp-idf/esp32s3/atk-dnesp32s3/dependencies.lock) | 无 |
| 板级组件 | 正点原子 BSP（板级支持包）、ES8388 和摄像头适配 | 无 | 无 |

组件的完整解析版本以工程依赖锁定文件和 TiRTC 组件 manifest（依赖清单）为准。

## 原理图与硬件资料

| 资料名称 | 版本或标识 | 链接 | SHA-256 | 资料状态 |
| --- | --- | --- | --- | --- |
| 正点原子官方板卡仓库 | 提交 `c7434a3da5b9e6feda05added5d6a686f1c95f13` | [GitHub](https://github.com/openedv/ATK-DNESP32S3-Board/tree/c7434a3da5b9e6feda05added5d6a686f1c95f13) | 无 | 已提供 |
| ATK-DNESP32S3 原理图 | V1.2 | [PDF](https://github.com/openedv/ATK-DNESP32S3-Board/blob/c7434a3da5b9e6feda05added5d6a686f1c95f13/1_docs/1_sch/ATK_DNESP32S3%20V1.2.pdf) | `f5226bc6324db6b65fe90f29e49c4c66ae03873297ea936c5a39f081d3378abf` | 已提供 |
| ATK-DNESP32S3 原理图 | V1.4 | | `020350b116ab6e39e6a807744b8749154efcd81b22df4b5a0169a4c7f61d0a64` | 未提供 |
| ATK-MC2640 摄像头原理图 | V2.2 | | `f681cd8967c89a274b488a7345728cd4826b9c9abf5b62049bfb92b8a4384baf` | 未提供 |
| ATK-MC5640 摄像头原理图 | V1.2 | | `9a490b8a7fb9701a933ecf9aa78df8dece70a2b67c235263152778b743f87e96` | 未提供 |

当前实板按 V1.4 适配。引脚和外设结论以工程 Hardware IR（硬件信息记录）及实板验证为准。

## 开发

### 目录结构

从仓库根目录看，这块板相关的代码分布如下：

```text
xiaotai/
├── boards/alientek/atk-dnesp32s3/          # 板卡身份、引脚与底层外设适配
├── product/                                # 跨芯片复用的业务状态、协议与测试
├── platforms/esp-idf/                      # ESP-IDF 公共平台实现
└── firmware/esp-idf/esp32s3/atk-dnesp32s3/
    ├── main/app_main.c                     # 启动入口和组件装配
    ├── components/starter_media/           # 音频、摄像头、按键手势和媒体管线
    ├── components/starter_runtime/         # 产品会话与运行状态协调
    ├── components/starter_tirtc/           # TiRTC 接口适配和音视频收发
    ├── components/platform_client/         # 服务发现、绑定、HTTP 和 MQTT
    ├── components/wifi_manager/            # 配网热点、网页和 Wi-Fi 连接
    ├── components/runtime_config/           # 持久化运行配置
    ├── components/starter_console/         # 串口诊断命令
    ├── third_party/tirtc/                   # 当前工程使用的 TiRTC 库
    ├── tests/                               # 固件主机测试
    └── *-contract.json                      # 硬件与媒体契约
```

`build/` 和 `build-relocated/` 是构建生成目录，不是源码入口。依赖组件通过工程配置更新，
不要直接修改 `managed_components/` 中的下载副本。

### 按任务查找代码

| 要修改的内容 | 首先查看 | 边界说明 |
| --- | --- | --- |
| 板卡型号、引脚、电源和外设初始化 | `boards/alientek/atk-dnesp32s3/board.json`、`board_config.h`、`atk_board.c` | 只放这块板特有的硬件事实 |
| 启动顺序和组件装配 | `firmware/esp-idf/esp32s3/atk-dnesp32s3/main/app_main.c` | 不在入口文件中堆叠业务规则 |
| H5、AI、设备呼叫和微信 VoIP 的互斥与状态 | `product/src/xiaotai_runtime.c`、`components/starter_runtime/` | 业务规则优先写进可测试的公共 C 接口 |
| 麦克风、扬声器、AEC、摄像头和 JPEG | `components/starter_media/`、`board-audio-contract.json`、`board-video-contract.json` | GPIO 和外设时序留在板级代码 |
| TiRTC 连接、流编号和音视频收发 | `components/starter_tirtc/`、`tirtc-runtime-contract.json` | 不把不同业务的流配置混用 |
| 配网、设备绑定、服务请求和 MQTT | `components/wifi_manager/`、`components/platform_client/` | 凭证和网络状态由对应组件管理 |
| 配置保存和串口排查命令 | `components/runtime_config/`、`components/starter_console/` | 新命令应有明确用途和稳定输出 |
| 回归测试和构建约束 | `product/tests/`、工程 `tests/`、`tools/tests/` | 修改行为时先补失败用例，再改实现 |

### 推荐修改顺序

1. 先读根目录 [`AGENTS.md`](../../../AGENTS.md)、[`ARCHITECTURE.md`](../../../ARCHITECTURE.md)
   和[测试说明](../../../tests/README.md)，确认代码应落在哪一层。
2. 涉及硬件时先核对工程中的 `hardware-ir.json` 和对应媒体契约，不能从相似板卡猜测引脚。
3. 先修改公共业务接口及其主机测试，再接入 ESP-IDF 组件；只有板卡差异才修改 `boards/`。
4. 运行本页“测试”中的命令，最后再上板验证真实麦克风、扬声器、摄像头和网络链路。

## 编译

```bash
bash tools/setup_esp_idf.sh esp32s3
. tools/activate_esp_idf.sh
python3 tools/build.py --board alientek-atk-dnesp32s3
```

工程目录为 `firmware/esp-idf/esp32s3/atk-dnesp32s3/`。

## 测试

```bash
bash tools/check.sh
bash firmware/esp-idf/esp32s3/atk-dnesp32s3/tests/run_host_tests.sh
python3 tools/build.py --board alientek-atk-dnesp32s3
```

实板至少验证启动、按键、配网、摄像头首帧、H5 双向语音和 AI 多轮对话。记录固件
提交号、BIN/ELF SHA-256、测试方向和首帧日志；主机测试不能替代麦克风、扬声器和
摄像头的实板检查。

## 排查

- 编译器或组件版本不一致：删除该工程生成的 `build/`，重新激活固定 ESP-IDF 后构建。
- H5 没有声音：分别记录设备到 H5、H5 到设备的流编号、首包计数和发送返回码。
- 摄像头无画面：先核对摄像头模组型号，再检查探测日志、JPEG 大小和流 11 的首帧。
- 配网失败：确认设备热点、`192.168.6.1` 页面和 STA 获取地址三步分别是否成功。

## 验证状态与已知限制

项目方确认当前固件已在实板跑通。TODO：补齐板卡正反面照片，并把验证结果绑定到
精确 BIN/ELF SHA-256、测试日期、启动日志和逐项功能矩阵。

板级配置和 adapter 位于 `boards/alientek/atk-dnesp32s3/`。准确引脚、器件和证据等级
以工程 `hardware-ir.json` 为准。
