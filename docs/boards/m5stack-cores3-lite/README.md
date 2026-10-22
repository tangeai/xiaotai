# M5Stack CoreS3-Lite 探鸽小钛

板卡 ID：`m5stack-cores3-lite`，商品编号 K128-Lite。用户已确认 M5 完成实板验证；具体构建身份及验证范围见[验证记录](../../../boards/m5stack/cores3-lite/VALIDATION.md)。
ESP32-S3 与立创相同，但接线、电源、功放、屏幕和触摸方向不同，固件不能互刷。

## 烧录

先按下文选择屏幕变体并编译，再在工程目录用原生 USB Serial/JTAG 烧录：

```bash
. tools/activate_esp_idf.sh
cd firmware/esp-idf/esp32s3/m5stack-cores3-lite
idf.py -p <确认过的串口> flash monitor
```

按官方说明，长按底侧复位键约 3 秒，绿色灯亮后松开进入下载模式；单击底侧 RST 复位。
左侧电源键单击开机、长按约 6 秒关机。首次烧录前核对产品型号和串口。
当前正式发布为 [v1.0.1 · build.42（ILI9342E）](https://github.com/tangeai/xiaotai/releases/tag/m5stack-cores3-lite-v1.0.1)。
用户已确认 M5 完成实板验证，开发基线 build.41 的歌曲嘴部恢复和运行日志保留；
完整功能范围、构建状态和已知问题见[封版记录](FREEZE.md)。

## 首次使用

设备没有 Wi-Fi 凭证时显示配网提示。使用者用手机或电脑连接 `XiaoTai-` 开头的开放热点，
在弹出页面填写目标 2.4 GHz Wi-Fi；没有弹窗时打开 `http://192.168.6.1`。
联网后屏幕显示六位绑定码，在 `https://xiaotai.chat` 注册或登录，选择“添加设备”并输入该码。
绑定成功后进入首页，使用触摸菜单进入 AI、联系人、多人对讲和设置。

Wi-Fi 连接失败时返回配网页重填；验证码过期等待刷新。更换网络或身份时从设置页选择
“重置设备”并确认，清除 Wi-Fi、绑定凭证和本机设置后重新配网绑定。凭证保存在 NVS，源码不带账号或密码。
上述为复用产品流程，仍需本板逐项验证。

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

复用当前 S3 产品媒体契约：H5 上行 MJPEG 320×240，目标上限 8 fps；实际帧率需测量。
H5 的 A-law 8 kHz 单声道音频使用流 10/14，视频流 11。AI 使用流 1、20 ms 单声道帧：
上行为 G.711 A-law / 8 kHz，每包 160 字节；下行为 Opus / 16 kHz。

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

持续对话需要重复或播放时难以打断时，build.15 可用 `voice-diag 30` 后接 `status`
采集同窗口播放、削顶、上行与 I2S 溢出观测，详见
[采音诊断](../../development/MEDIA_PIPELINE_DIAGNOSTICS.md#cores3-持续对话采音观测)。
该版本只增加观测，不代表声学问题已经修复。

AI 上行格式由 ESP 共享内部配置 `CONFIG_STARTER_AI_UPLINK_G711A=y` 固定。
AI start_session 请求使用官方协议名 `alaw`，响应兼容 `g711a` 别名。
它同步控制 start_session 输入声明/响应校验、预录音/实时 PCM、编码器选择和
TiRTC 的 A-law / 8 kHz 帧标志；输出声明及解码仍为 Opus / 16 kHz。

AEC 继续在 16 kHz 域运行，再复用既有低通降采样结果。为了只比较编码开销，
保留原来的 codec 初始化、缓存容量、任务调度、增益和音量设置。
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
已保存用户原有资料和新增官方资料的本地副本。

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
独立 `BUILD_NUMBER` 每次编译递增，失败也消耗编号，不改变产品版本 `1.0.1`。

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

新增测试覆盖构建变体与版本、真实电源接口的位保护及失败传播、MIC/回采槽位整理、
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

当前主机检查和默认 E 屏目标编译记录见下方验证记录。用户 build.9 日志已确认
8 MB PSRAM / 80 MHz、内存测试通过和应用启动，但照片仍有花屏。
build.10 修正共享 UI 覆盖面板方向；用户已绑定并进入 AI，随后反馈触摸暂时无响应和
偶发自回声。四角触摸及声学验收仍未完成。build.11 修正串口绑定网址，
与屏幕统一为 `https://xiaotai.chat`；build.12 按用户要求旋转显示与触摸 180°。
AEC、增益或调度配置未修改。

偶发自回声先对照麦克风灵敏度 4（33 dB）与 2（24 dB），其余条件不变。
AI 播放期间执行 `voice-diag 10`，前后各取一次 `status`，核对削顶和超时增量；
先测远端独自播放，再测人声和双讲。具体参考与未采用项见
[音频比较](../../../boards/m5stack/cores3-lite/SOURCES.md#build10-自回声反馈后的专项比较)。

准确路径、BIN/ELF 校验值和验证范围见
[验证记录](../../../boards/m5stack/cores3-lite/VALIDATION.md)。旧 build.6 / build.7 不应再烧录。
本次未由代理烧录、未发布 Release，不能继承其他板卡的通过结论。

TODO：确认 PCB/屏幕实物型号；同一固件 SHA-256 下验证启动、屏幕颜色及四角触摸、
配网绑定、GC0308 画面方向、H5 双向音频、AI、设备/微信语音通话、房间 PTT、
AEC 远端单讲/近端单讲/双讲、重复切换、断网恢复及长时间内存稳定性。
SD 卡、IMU、磁力计、接近传感器和 RTC 不属于当前小钛业务接入范围。

build.23 播放期间的任务采样、缓存/内部 RAM 预算、官方性能资料和后续单变量
验证顺序见[音频性能排查](AUDIO_PERFORMANCE_REVIEW.md)。该报告使用 ESP-IDF v5.5.5、
ESP-SR 2.4.7 的证据，尚未宣称解决播放期间的采集溢出或 UI 卡顿。
