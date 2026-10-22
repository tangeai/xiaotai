# 觅感 AI 玩具开发板 BK7258

[返回开发板目录](../README.md) · [下载固件及发布说明](https://github.com/tangeai/xiaotai/releases/tag/mgkfb-bk7258-v1.0.1)

板卡 ID：`mgkfb-bk7258`。本板支持 AI 对讲、微信 VoIP（网络语音通话）和 H5（网页端）
实时查看。两块屏幕分别显示左右眼，随 AI 情绪标签切换表情；配网、绑定和通话时显示操作提示。
操作使用实体按键，不支持屏幕触摸、设备互呼和多人对讲。

## 烧录

当前发布版本为 **v1.0.1 · build.11**，适用于 **8 MiB Flash 的觅感 BK7258 双屏板**。
不能使用立创 BK7258、ESP32 或厂家 AIDK 工程的固件。

1. 下载并解压 [`mgkfb-bk7258-1.0.1+build.11-validation.zip`](https://github.com/tangeai/xiaotai/releases/download/mgkfb-bk7258-v1.0.1/mgkfb-bk7258-1.0.1%2Bbuild.11-validation.zip)。
2. 用本板通讯 Type-C 连接电脑，关闭占用串口的日志终端，在 Beken 下载工具中选择对应 COM 口和 BK7258。
3. 选择 **`mgkfb-bk7258-1.0.1+build.11-all-app.bin`**，起始地址为 **`0x00000000`**。
4. 启动下载，按工具提示复位或重新上电完成下载握手；等待写入及工具校验完成，再重新上电。
5. 打开 115200 波特率日志，核对板卡 `mgkfb-bk7258` 和版本 `1.0.1+build.11`，再进行配网与绑定。

| 文件 | 用途 |
|---|---|
| `mgkfb-bk7258-1.0.1+build.11-all-app.bin` | 启动程序、CP 和 AP 的完整镜像，用于首次串口烧录，地址 `0x0` |
| `mgkfb-bk7258-1.0.1+build.11-app_pack.rbl` | OTA（无线升级）镜像，不能代替完整串口镜像；本板 OTA 尚未专项验收 |
| `MANIFEST.json` | 板卡、版本、文件长度、SHA-256 校验值及烧录地址 |
| `symbols/`、`partitions/`、`build_identity.json` | 故障定位、分区核对和构建身份记录 |

ZIP 是分发包，不能直接烧录。完整 BIN 的 SHA-256 为：

```text
5a8d24a9d058822e41b2700c325b924c6958385ce2d54e0aa4bbf05511dc90fb
```

烧录接口使用 UART0（TX GPIO11、RX GPIO10），不使用 ESP32 的网页烧录工具。
首次烧录前保存原厂固件及射频校准资料，核对分区，不盲目全片擦除。

## 首次使用

1. 设备没有 Wi-Fi 配置时，屏幕显示配网热点名。使用手机连接该热点，再用手机浏览器打开
   `http://192.168.6.1`，填写家中 Wi-Fi 信息。保存后等待设备联网；连接失败时重新连接热点检查配置。
2. 联网后，屏幕显示绑定验证码并语音播报。使用手机打开 `https://xiaotai.chat`，注册或登录账号，
   在“添加设备”中输入设备显示的验证码。以平台提示绑定成功、设备恢复眼睛显示为完成；
   验证码过期时使用设备刷新的新码。
3. 单击 S2 开始 AI 对讲，再单击结束；按下表进行音量和微信通话操作。
4. 需要实时查看时，在手机网页中打开已绑定设备的实时查看入口。画面来自本板摄像头，
   不依赖眼睛屏幕；没有画面时按“排查”一节核对相机日志。

| 按键 | 操作 |
|---|---|
| S2 单击 | 空闲时开始 AI；AI 中结束；微信来电时接听；通话中挂断 |
| S2 双击 | 空闲时向平台排序的首个联系人发起微信呼叫；微信来电时拒接 |
| S1 单击 | 音量加一档，最高 10 档 |
| S3 单击 | 音量减一档，最低 0 档 |
| S1 长按 | 空闲时进入配网；正在使用 AI、实时查看或通话时不进入配网 |

S2 单击需要等待双击判定；S1/S3 在去抖松开后立即调节，连续点击逐次生效。
build.11 的屏幕若提示 `192.168.4.1`，请在手机浏览器使用正确地址 `http://192.168.6.1`。
快捷呼叫只使用平台排序的 `contacts[0]`；没有联系人或首项不支持微信 VoIP 时提示失败，
不会自动改拨其他联系人。AI 中先单击结束，再双击呼叫。

## 规格

| 项目 | 当前配置与依据 |
|---|---|
| 开发板 | 觅感 AI 玩具开发板 BK7258；实物 PCB 版本待核对 |
| 平台 | Beken AVDK SMP（对称多处理）AP/CP 架构 |
| 程序 Flash | 8 MiB，用户确认；分区末端 `0x800000`，未读取 JEDEC 器件 ID |
| PSRAM（外部运行内存） | 物理容量待测；不能用可用堆大小代替物理容量 |
| 显示 | 两块 GC9D01 SPI 160×160 屏，各显示一只眼睛，不显示嘴巴 |
| 输入 | S1 GPIO13、S2 GPIO12、S3 GPIO8，低电平有效；无触摸 |
| 音频 | 模拟麦克风；功放 GPIO50，共享外设电源 GPIO52 |
| 摄像头 | GC2145，DVP（并行视频）接口；已在日志读出器件 ID `0x2145` |
| 资源存储 | 规格书的 128 MB 指 SD NAND，不是程序 Flash；当前功能不依赖该存储 |

完整引脚及证据等级见 [Hardware IR](hardware-ir.json)。
配网和绑定正文使用 16 像素字库，每屏约容纳 8 个汉字一行、6 行文字；地址使用较窄字距。

## 音视频参数

AI 上行为 A-law 8 kHz，下行为 Opus 16 kHz，保留 AEC（回声消除）。
H5 摄像头上行为 H.264，GC2145 配置目标为 640×480 / 20 fps；微信 VoIP 仅启用音频。
实际参数及不同业务的媒体编号以[音视频合同](../../product/MEDIA_CONTRACT.md)为准。

## 环境与依赖

- Beken AVDK SMP `release/v3.1.1.10`，提交 `398cab28b938e69ba2071a597daf00845dd080b6`。
- ARM GCC `10.3-2021.10`，工程沿用仓库 Beken 环境。
- TiRTC 库位于 `platforms/beken/third_party/tirtc`；编译器与库的二进制接口要求见[来源说明](../../../firmware/beken/bk7258/lckfb-bk7258-xiaotai/SOURCE_PROVENANCE.md)。

AIDK 只用于核对硬件和驱动，不作为本工程的构建依赖，不混用其二进制库或分区。

## 原理图与硬件资料

觅感《AI 玩具开发板规格书 V1.0》由用户提供，公开下载地址未提供。
文件校验值与接线依据记录在 [Hardware IR](hardware-ir.json)，V1.0 为文档版本，不能代替实物 PCB 版本。
Beken 参考资料见[官方硬件资料](https://docs.bekencorp.com/arminodoc/bk_aidk/bk7258/zh_CN/v2.0.1/hw-reference/index.html)。

## 开发目录

| 目录 | 用途 |
|---|---|
| `boards/mgkfb/bk7258/` | 板卡登记、GPIO、供电、按键、音频、相机与屏幕驱动 |
| `firmware/beken/bk7258/mgkfb-bk7258-xiaotai/` | 本板原生工程、AP/CP 配置、分区及版本号 |
| `platforms/beken/` | 公共运行时、平台适配、构建脚本和 SDK 补丁 |
| `product/` | 产品会话、AI 标签、交互模型及公共渲染逻辑 |

交互适配器只发送意图和显示状态，联网、绑定、AI 与通话状态由产品运行时管理。

## 编译

在仓库根目录执行：

```bash
python3 tools/build.py --validate
python3 tools/build.py --board mgkfb-bk7258
```

本板使用固定的原生输出目录：

```text
firmware/beken/bk7258/mgkfb-bk7258-xiaotai/build/bk7258/mgkfb-bk7258-xiaotai/
├── bk7258_ap/         AP 编译结果
├── bk7258/            CP 编译结果
├── partitions/        分区配置
├── build_identity.json
└── package/
    ├── all-app.bin
    └── app_pack.rbl
```

每次构建更新这个目录，功能配置不改变工程名或目录名。`build_identity.json` 记录本次
版本、配置和校验值；不要把目录里保留的旧版本具名文件当作本次构建结果。
构建号在本板 `BUILD_NUMBER` 中递增，产品版本保持不变。

正式发布使用以下命令，编译和打包必须选择相同配置：

```bash
python3 tools/build.py --board mgkfb-bk7258 --release
python3 tools/package_validation_firmware.py --board mgkfb-bk7258 --beken-build-profile release
```

发布配置仍使用同一个输出目录。打包器读取构建身份；如果当前目录是其他配置，先重新编译，
不能靠改文件名代替。完整发布流程见[参与开发](../../../CONTRIBUTING.md#固件版本与发布)。

## 测试

```bash
python3 -m unittest discover -s tools/tests -v
bash tools/check.sh
```

主机测试覆盖按键手势、AI 表情、配网文字、双屏缓冲所有权、媒体合同、板卡隔离和打包身份。
涉及 GPIO、屏幕、音频或相机的修改，还需在本板检查显示、配网绑定、AI、微信通话和 H5 实时查看。

## 排查

- **无法配网**：检查启动日志是否进入网络初始化；已有 Wi-Fi 配置时会优先连接，空闲长按 S1 可重新配网。
- **屏幕不亮**：先核对固件板卡和配置，再检查 GPIO52 供电及 GPIO25 背光。LCD1 为左眼，QSPI1 GPIO2/3/4、DC5、RESET45；LCD2 为右眼，QSPI0 GPIO22/23/24、DC7、RESET6。
- **相机无画面**：检查 GC2145 ID 及发送帧数。相机电源 GPIO49、复位 GPIO28、控制总线 GPIO42/43；DVP 引脚 GPIO27、29…39 需要正确复用。
- **无声音或音量不变**：单击 S1/S3，核对 `volume key` 日志中的音量前后值、静音标志和保存错误；再检查供电 GPIO52 与功放 GPIO50。
- **快捷微信呼叫失败**：确认已绑定、联系人已同步，且平台排序的首项支持微信 VoIP。

## 验证状态与已知限制

用户已确认本板功能实测通过，并明确确认音量可调。发布构建、镜像校验和专项范围见
[验证记录](VALIDATION.md)，不把功能实测结论扩大为所有异常与长期稳定性均已验收。

不支持触摸、设备互呼和多人对讲。OTA、长期运行、弱网、双讲 AEC、音量边界与重启保存、
双屏 DMA（直接内存访问）异常恢复仍需专项验证；来电和错误的完整语音引导待完善。
Flash JEDEC、物理 PSRAM、回采电阻及功放时序尚无量测报告。
