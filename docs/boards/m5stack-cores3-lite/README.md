# M5Stack CoreS3-Lite

[返回开发板目录](../README.md) · [固件下载与发布说明](../../downloads.md#m5stack-cores3-lite)

板卡 ID：`m5stack-cores3-lite`。两种屏幕固件的当前版本见[固件下载](../../downloads.md#m5stack-cores3-lite)。
短句识别、AI 打断和双讲效果仍需回归；详情见[验证状态与已知限制](#验证状态与已知限制)。

## 烧录

文件名中的 `<版本>` 代表下载页列出的完整版本（含 build 编号）。操作时选择实际下载的文件，并核对包内清单；截图中的版本和路径仅作示例。

### 烧录前确认屏幕型号

先核对实物是否为 **CoreS3-Lite K128-Lite**，再向购买渠道或 M5Stack 确认屏幕控制器是
`ILI9342E` 还是 `ILI9342C`，以明确的器件型号为准。不能仅凭购买日期、产品名或外壳外观推断；
官方资料说明同系列存在两种面板，[M5GFX 的面板识别实现](https://github.com/m5stack/M5GFX/releases/tag/0.2.30)
也分别处理 C/E。无法确认时先向销售方索取屏幕型号，再从下表选择对应固件。

| 屏幕控制器 | 完整镜像（首次烧录） | 分文件包（保留配置升级） |
|---|---|---|
| ILI9342E | 当前可用性见[固件下载](../../downloads.md#m5stack-cores3-lite) | 当前可用性见[固件下载](../../downloads.md#m5stack-cores3-lite) |
| ILI9342C | [完整镜像（BIN）](../../downloads.md#m5stack-cores3-lite) | [固件包（ZIP）](../../downloads.md#m5stack-cores3-lite) |

### 打开烧录工具

使用电脑上的 **Chrome 或 Edge** 打开[乐鑫 ESP Web Tool](https://espressif.github.io/esptool-js/)；
网页烧录无需安装 ESP-IDF。关闭占用设备串口的终端或其他烧录网页。
接口和下载模式位置见[板卡官方说明](https://docs.m5stack.com/en/core/CoreS3-Lite)。

### 烧录步骤

1. 从上表按屏幕控制器下载对应的完整 `merged.bin`。烧录时只选择这一个 BIN，地址为 **`0x0`**。
2. 使用本机 Type-C 接口连接电脑。长按底侧复位键约 3 秒，绿色灯亮后松开，灯熄灭后进入下载模式。识别结果必须是 **ESP32-S3**。
3. 在网页工具的 **Program** 区域选择 `Baudrate`（可先用 `921600`，不稳定时改为 `115200`），点击 **Connect** 并选择本板串口。桌面串口方式不勾选 `WebUSB (CH340)`。
4. 点击 **Choose File** 选择下载的完整 BIN，**Flash Address 改为 `0x0`**；Flash Mode、Flash Frequency、Flash Size 均选 `keep`。只添加这一行，点击 **Program**。
5. 等待每个文件显示 `Hash of data verified.`，最终显示 `Leaving...` 且没有 `Error:`。点击 **Disconnect**，按板卡复位键重启，用 115200 波特率日志核对项目 `m5stack_cores3_lite_xiaotai`，以及下载包对应屏幕变体和完整版本，再进入“首次使用”。

连接位置和烧录参数见下图，图中的端口号与路径只是示例：

![ESP Web Tool 的 Connect 和 Program 区域](../../images/flashing/web-connect.png)

![M5Stack CoreS3-Lite 完整镜像的文件、0x0 地址与 Program 按钮](../../images/flashing/web-m5stack-cores3-lite.png)

**完整镜像会覆盖 NVS 中的 Wi-Fi、绑定和 EP 覆盖项等持久配置。** 烧录后需重新配网、绑定。
`keep` 仅保留镜像中的 Flash 参数，不表示保留设备配置。若网页连接失败，按第 2 步进入下载模式后重连；
若写入或校验失败，检查串口、数据线、文件和地址后重试，不按进度条 100% 判定成功。

| 下载或包内文件 | 用途 |
|---|---|
| 上表对应变体的 `-merged.bin` | **直接烧录**，只此一个文件，地址 `0x0` |
| 上表对应变体的 `-validation.zip` | 分文件烧录包，先解压，按下表各地址写入 |
| `m5stack_cores3_lite_xiaotai.bin` | 应用 BIN，仅在分文件方式中写入 `0x10000`，不可当完整镜像写入 `0x0` |
| `MANIFEST.json`、`flasher_args.json`、`flash_args`、ELF | 清单或排查资料，不直接烧录 |

完整 BIN 的 SHA-256 见下载页对应 Release 的 `SHA256SUMS`。

Windows 可用 `Get-FileHash -Algorithm SHA256 "实际BIN路径"`；Linux 用 `sha256sum "实际BIN路径"`；
macOS 用 `shasum -a 256 "实际BIN路径"`。分文件的哈希见包内 `MANIFEST.json`，
发布附件的完整哈希见上表对应发布页中的 `SHA256SUMS`。

## 首次使用

确认完整镜像已从 `0x0` 写入且设备正常重启，再进行配网和绑定。若改用分文件包，
需确认清单中的 3 个 BIN 都已按各自地址写入；两种方式任选一种。

1. **配置 Wi-Fi**：设备没有 Wi-Fi 凭证时会显示配网提示。用手机或电脑连接 `XiaoTai-`
   开头的开放热点；提示“无互联网”属于正常现象。在弹出页面选择目标 2.4 GHz Wi-Fi，
   输入密码并提交。没有弹窗时打开 `http://192.168.6.1`。热点重新出现表示联网失败，
   请检查密码和信号后重试。
2. **绑定设备**：联网后屏幕显示六位绑定码。让手机或电脑恢复互联网连接，打开
   [小钛设备页](https://xiaotai.chat/devices)，注册或登录，在“添加设备”中输入屏幕上的码。
   验证码过期时使用屏幕刷新后的新码。设备进入首页且出现在设备列表中，表示绑定成功。
3. **体验与恢复**：从触摸菜单进入 AI、联系人、多人对讲和设置。更换网络或重新绑定时，
   在设置页选择“重置设备”并确认，再重新配网绑定；不要擦除整片 Flash。

Wi-Fi 和绑定凭证保存在 NVS，源码不带账号或密码。

## 其他烧录方式

### 分文件烧录

从上表下载并解压与屏幕控制器对应的 ZIP。
此方式需按包内 `MANIFEST.json` 和 `flasher_args.json` 核对本板、本版的 **3 个 BIN**：

| 文件 | 烧录地址 | 用途 |
|---|---|---|
| `bootloader/bootloader.bin` | `0x0` | 启动引导程序 |
| `partition_table/partition-table.bin` | `0x8000` | 分区表 |
| `m5stack_cores3_lite_xiaotai.bin` | `0x10000` | 应用程序，不可写入 0x0 |

在网页工具用 **Add File** 添加上述每个 BIN，各填对应地址，然后点击 **Program**。
不要同时添加完整镜像；不同板卡的 bootloader 地址也可能不同。分文件方式可用于保留配置的升级，
但必须先确认分区布局兼容，不执行全片擦除。操作位置见下图：

![M5Stack CoreS3-Lite 分文件烧录的文件与地址](../../images/flashing/web-split-m5stack-cores3-lite.png)

### 从源码编译后烧录

按[源码获取说明](../../../README.md#从源码编译)准备仓库，并安装本板要求的 ESP-IDF。
在仓库根目录执行：

```bash
. tools/activate_esp_idf.sh
python3 tools/build.py --board m5stack-cores3-lite --variant ili9342e
cd firmware/esp-idf/esp32s3/m5stack-cores3-lite
idf.py -p <串口> flash monitor
```

`<串口>` 换成本机实际端口；`idf.py` 会使用工程烧录清单和正确地址。
普通升级不执行 `erase-flash`。示例命令截图不代表已在当前硬件执行：

![M5Stack CoreS3-Lite 工程编译及烧录命令](../../images/flashing/project-m5stack-cores3-lite.png)

## 规格

| 项目 | 配置 |
|---|---|
| 芯片 / 当前框架 | ESP32-S3 / **ESP-IDF v5.5.5** |
| 存储 | 16 MB Flash / 8 MB Quad PSRAM，PSRAM 配置 80 MHz |
| 屏幕 / 触摸 | 320×240 ILI9342C 或 ILI9342E / FT6336U |
| 摄像头 | GC0308，板载 24 MHz 时钟 |
| 音频 | ES7210 双麦输入及电气回采；AW88298 + 1 W 扬声器 |
| 电源 | AXP2101 + AW9523B，200 mAh 电池（官方规格，未测容量） |
| 交互 | `touch-full` / `compact`，16 像素中文字库 |

## 音视频参数

两种屏幕的当前正式版见[固件下载](../../downloads.md#m5stack-cores3-lite)。
两版使用相同媒体参数。音频均为单声道，每包 20 毫秒。

| 场景 | 方向与流编号 | 编码与采样率 | 包大小或画面参数 |
|---|---|---|---|
| AI 对讲 | 上行、下行：流 1 | 上行 A-law / 16 kHz；下行 Opus / 16 kHz | 上行 320 字节；Opus 包长可变 |
| H5 实时对讲 | 上行：流 10；下行：流 14 | 双向 A-law / 8 kHz | 每包 160 字节 |
| 设备呼叫、微信语音通话 | 双向：音频流 0 | 双向 A-law / 8 kHz | 每包 160 字节 |
| H5 实时画面 | 上行：流 11 | MJPEG（逐帧 JPEG） | 320×240，目标上限 8 fps；实际帧率需测量 |

设备间呼叫和微信 VoIP 均为纯音频。微信能力上报 `up_video_mt=none`、
`down_video_mt=none`、`no_video=true`，双向 A-law / 8 kHz 使用音频流 0。
首页微信快捷呼叫提交 `wx_room_type=voice`，联系人详情仅提供“语音通话”。
S3 平台策略同时约束 runtime、UI、摄像头任务与 SDK 发送入口；即使误传 video 请求
或存在旧视频订阅，也不得启动 VoIP 视频上行。立创 S3 同样为纯音频通话，P4 保留原能力。
房间 PTT 沿用公共房间运行时。

摄像头朝上使用时，设备能力的 `stream/call/voip.camera_rotation` 以及设备呼叫
请求、接听信息均上报数值 `180`。H5 接收端据此旋转原始 MJPEG 画面；
固件不再额外旋转摄像头像素。语音呼叫保留的方向元数据不代表视频能力；
M5 设备间呼叫和微信 VoIP 均不发送视频，H5 实时查看保留视频。
升级联网后会重新提交能力；关闭旧的实时查看页面，再重新进入验证画面方向。

硬件采集和播放固定 16 kHz。I2S0 的四槽 TDM RX 与双槽标准 I2S TX 成对创建，
共享 64fs BCLK、256fs MCLK；AW88298 的 BCLK 配置写后读回。
AW88298 按乐鑫官方 CoreS3 BSP 配置 15 dB PA 增益补偿，用于正确映射音量衰减；
补齐后相同音量设置可能比早期适配更轻，实板音质和双讲效果仍待确认。

ES7210 前两线槽是 MIC1 人声和 MIC3 扬声器电气回采，板级整理成 MR 后进入公共 ESP-SR AEC。
原理图只能证明参考接线，回声效果、削波、双讲和声学延时必须实测。
官方源码核对、驱动增益量化及当前证据边界见
[音频核查](../../../boards/m5stack/cores3-lite/OFFICIAL_AUDIO_REVIEW.md)。

持续对话需要重复或播放时难以打断时，可用 `voice-diag 30` 后接 `status`
采集同窗口播放、削顶、上行与 I2S 溢出观测，详见
[采音诊断](../../development/MEDIA_PIPELINE_DIAGNOSTICS.md#cores3-持续对话采音观测)。
诊断命令只读取统计数据，不修改音频参数。

AI 上行格式由 ESP 共享内部配置 `CONFIG_STARTER_AI_UPLINK_G711A_16K=y` 固定。
AI start_session 请求使用官方协议名 `alaw`，响应兼容 `g711a` 别名。
它同步控制 start_session 输入声明/响应校验、预录音/实时 PCM、编码器选择和
TiRTC 的 A-law / 16 kHz 帧标志；输出声明及解码仍为 Opus / 16 kHz。

AEC 继续在 16 kHz 域运行，AI 上行直接编码处理后的 16 kHz PCM；
H5 和通话仍使用原有 8 kHz 路径。
服务端若拒绝该输入/输出组合，会报协商错误并停止，不能视为声学测试通过。
立创 S3、微雪 P4 和 BK7258 现在采用相同 AI 网络格式，各板保留原有 DSP 处理。

## 环境与依赖

当前应用工具链固定 **ESP-IDF v5.5.5**，提交
`b774170ff46c393eeb5e495ea37936038d3f4f4f`。从仓库根目录执行
`. tools/activate_esp_idf.sh` 后用 `idf.py --version` 核对；缺少工具时使用
`bash tools/setup_esp_idf.sh esp32s3` 安装仓库指定版本。
组件声明的 `>=5.5,<5.6` 只是兼容范围，不是本次使用版本。

TiRTC SDK 2.5.0 使用本工程 `third_party/tirtc`，受其 ABI 构建合同约束。
LVGL 8.3.11、esp_lvgl_port 2.7.2、esp32-camera 2.1.7、esp-sr 2.4.7、
esp_audio_codec 2.4.0、esp_lcd_ili9341 2.1.0；全部实际解析版本及哈希见 `dependencies.lock`。
编译不读取 `.references` 或用户提供的旧工程。

## 原理图与硬件资料

官方入口：[CoreS3-Lite](https://docs.m5stack.com/zh_CN/core/CoreS3-Lite)。
[原理图 v1.0](https://m5stack-doc.oss-cn-shenzhen.aliyuncs.com/490/Sch_M5_CoreS3_v1.0.pdf)、
数据手册、BSP 及下载 SHA-256 见[资料与参考取舍](../../../boards/m5stack/cores3-lite/SOURCES.md)。

当前官方页面的音频 GPIO13/14 方向与原理图冲突；实现采用原理图和官方 BSP 一致的
RX14/TX13。2026-08-07 起官方屏幕由 C 改 E，不能从产品名确定实物屏幕。

## 开发

板级代码在 `boards/m5stack/cores3-lite/`：`board_power.c` 管电源和复位，
`board_adapter.c` 管音频与摄像头，`board_display.c` 管屏幕和触摸。
`hardware-ir.json` 保存来源、引脚和证据等级。S3 媒体编排在
`platforms/esp-idf/components/starter_media_s3/`，其余运行时、UI、配网等直接使用公共组件。

参考旧 `m5stack-cores3-se-xiaotai` 的电源时序、codec 初始化、AW88298 时钟读回和
ILI9342C/FT6336 硬件初始化；原因是器件和接线经资料核对一致。没有移植旧业务状态机、
UI、AGC 或 ISR 队列；具体文件和采用原因见[参考说明](../../../boards/m5stack/cores3-lite/SOURCES.md)。

GPIO0 用作音频 MCLK，不作为业务按键。电源键/RST 保留硬件功能，业务由触摸发出意图。
房间分配仅由 MQTT 通知和进入多人对讲页刷新，不增加启动或定时查询。

显示方向由板级 `LCD_SWAP_XY`、`LCD_MIRROR_X/Y` 定义。按摄像头位于顶部的握持方向，
CoreS3 设置 swap=false、mirror_x=true、mirror_y=true，原生横屏旋转 180°。
面板、触摸与共享 LVGL 注册使用同一组值，避免 UI 初始化覆盖原生 320×240 地址方向。

## 编译

M5 工程通过组件私有定义 `XIAOTAI_UI_TEXT_PSRAM=1`，将 AI 字幕历史（2048 字节）
和显示文本（2304 字节）放入 PSRAM 的静态 BSS，减少 4352 字节内部静态占用。
它采用 [ESP-IDF v5.5.5 外部 BSS 机制](https://github.com/espressif/esp-idf/blob/v5.5.5/docs/en/api-guides/external-ram.rst)，
保留固定容量、开机清零和整个 UI 生命周期，不增加每轮对话的分配/释放。

有效配置必须开启 `CONFIG_SPIRAM_ALLOW_BSS_SEG_EXTERNAL_MEMORY`，否则编译拒绝。
立创 S3/P4 未启用这个定义；DMA、锁、任务栈与 LVGL 对象池保持原分配策略。
静态占用降低不等于运行时连续堆块等量增加，也不代表 AEC 耗时降低；需同版实测。

在仓库根目录执行，一次只选一种屏幕：

```bash
. tools/activate_esp_idf.sh
idf.py --version
python3 tools/build.py --board m5stack-cores3-lite --variant ili9342e
# 确认是旧 ILI9342C 屏时改用：
python3 tools/build.py --board m5stack-cores3-lite --variant ili9342c
```

省略 `--variant` 选择 `ili9342e`。切换会重新配置面板命令表，不会运行时猜测屏幕。
原生输出在 `firmware/esp-idf/esp32s3/m5stack-cores3-lite/build/`，应用为
`m5stack_cores3_lite_xiaotai.bin` / `.elf`；烧录地址以同目录 `flasher_args.json` 为准。
独立 `BUILD_NUMBER` 每次编译递增，失败也消耗编号，不自动改变产品版本。

原生 CMake 检查最终生效的 PSRAM 配置，拒绝 Octal 或关闭 PSRAM 的构建。
若已有 `sdkconfig` 保存了旧的 Octal 设置，修改 `sdkconfig.defaults` 不会覆盖它：
在工程目录执行 `idf.py menuconfig`，进入 `Component config → ESP PSRAM`，
将模式改为 Quad，保存后通过上述统一入口重新编译。保留其他本机设置，无需擦除 NVS。

## 测试

```bash
python3 tools/tests/test_cores3_board.py
python3 tools/tests/test_cores3_memory_config.py
python3 tools/tests/test_s3_display_geometry.py
python3 tools/build.py --validate
python3 -m unittest discover -s tools/tests -v
bash tools/check.sh
```

主机测试覆盖构建变体与版本、真实电源接口的位保护及失败传播、MIC/回采槽位整理、
两版面板命令和 GPIO0 排他性。PSRAM 回归检查新建工程默认值，并执行真实 CMake
检查入口，确认旧 Octal 配置、关闭 PSRAM 和冲突配置均被拒绝。
公共 S3 回归覆盖既有会话、音频、尾音和 UI 行为；
源码复用不等于本板的网络或声学测试已通过。

## 排查

若启动出现 `octal_psram: PSRAM chip is not connected, or wrong PSRAM line mode`
及 `Failed to init external RAM`，说明程序在 `app_main` 之前初始化外部 RAM 失败。
先核对有效配置必须为 `CONFIG_SPIRAM_MODE_QUAD=y`，且 Octal 关闭。
仅按 RST 或恢复网络设置不能修正已烧录固件的总线模式；需重新编译并烧录。
保持 PSRAM 初始化与内存测试开启，不用忽略 PSRAM 错误的选项绕过启动检查。

屏幕不亮先确认 C/E 变体、PMIC 背光和 LCD 复位，再检查 SPI；不要套用立创背光 GPIO42。
文字错行或画面只占部分区域时，检查 `cores3_display` 启动日志应为
`native=320x240 UI=320x240 swap_xy=0 mirror=1/1`，并与实物 C/E 面板核对。

触摸异常检查 I²C0 SDA12/SCL11 和 0x38，屏幕原生横向坐标不交换。
摄像头初始化失败检查 GC0308 PID、外部时钟及 AW9523/AXP 电源，不接受其他型号静默替代。
无声先检查 RX14/TX13、codec 地址、AW88298 寄存器 0x06 的 64fs 读回，
再做仅人声、仅扬声器和双讲三种槽位测试。GPIO0 不允许配置为按键输入。

## 验证状态与已知限制

1.1.0 测试固件新增 AI 音频分析：原始 MIC1 的 16 kHz PCM 触发后保留最多 5 秒预录，
再录 10 秒上传。首页 AI 图标支持开关，底色表示 60 秒冷却，状态显示录制和上传阶段，
结果滚动两次并语音播报。通话、实时查看、AI 对话和房间业务占用期间暂停识别。
MIC3 回放参考不进入识别录音，阈值与实际声学表现仍待 M5 实板验证。

M5 已完成实板验证，已验证的发布记录为 **v1.0.1 · build.47 / ILI9342E** 和
**v1.0.1 · build.48 / ILI9342C**。
功能与修复列表见[发布记录](FREEZE.md)，具体构建身份、验证范围和历史日志见
[验证记录](../../../boards/m5stack/cores3-lite/VALIDATION.md)。

- 收音、AI 打断和双讲效果仍需持续测试；短句“好的”识别不稳定、可能丢失句首的问题尚未修复。
- 本版不提供录音导出到 TF 卡的实验功能。诊断使用 `voice-diag`、`status` 和 `face-animation`。
- 播放期间的 AEC 与 UI 实时处理余量仍偏紧；弱网、反复切换和长时间运行需持续回归。分析见[音频性能排查](AUDIO_PERFORMANCE_REVIEW.md)。
- 按屏幕控制器选择对应固件，当前版本见下载页。
- SD 卡、IMU、磁力计、接近传感器和 RTC 尚未接入小钛产品功能。
