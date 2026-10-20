# CoreS3-Lite 硬件资料与实现取舍

资料获取日：2026-10-20。使用 `tirtc-esp32-builder` 0.10.1；板库匹配结果为 `none`，
没有套用立创或 CoreS3-SE 的板卡身份。下面的下载副本均只在被忽略的 `.references/` 保存；
编译只依赖仓库源码、锁定组件和 TiRTC SDK。

## 官方资料

| 文件 | 官方来源 | SHA-256 |
|---|---|---|
| `bsp_audio.c` | [下载](https://raw.githubusercontent.com/espressif/esp-bsp/master/bsp/m5stack_core_s3/src/bsp_audio.c) | `29f7b11377132cd10f54fd090cdc029d9d0b0b3e14ab0d52ead47c3f5067a244` |
| `bsp_camera.c` | [下载](https://raw.githubusercontent.com/espressif/esp-bsp/master/bsp/m5stack_core_s3/src/bsp_camera.c) | `d034dc4eda55a7c412ef6e096889aff5008f691d187a1ff5303df732d4b3b28b` |
| `bsp_display.c` | [下载](https://raw.githubusercontent.com/espressif/esp-bsp/master/bsp/m5stack_core_s3/src/bsp_display.c) | `4670c095dc02ce7c4ebf6c1ae5e974eb39d0c30ca5e23163ba30fe9f924c10f8` |
| `bsp_feature_en.c` | [下载](https://raw.githubusercontent.com/espressif/esp-bsp/master/bsp/m5stack_core_s3/src/bsp_feature_en.c) | `7afb947602329f772e2a4834eb5060d5af7add34e8d4533d6fec9950203076cb` |
| `bsp_i2c.c` | [下载](https://raw.githubusercontent.com/espressif/esp-bsp/master/bsp/m5stack_core_s3/src/bsp_i2c.c) | `aa0f2549e262f144f0751d49b69561b8a1846cd14d249713afa503c1b20e6e83` |
| `bsp_io_expander.c` | [下载](https://raw.githubusercontent.com/espressif/esp-bsp/master/bsp/m5stack_core_s3/src/bsp_io_expander.c) | `e08eb4bfef3149945210a512294ff33e4bcdcf8aa8ee6331374e54b9f423ba12` |
| `m5stack_core_s3.h` | [下载](https://raw.githubusercontent.com/espressif/esp-bsp/master/bsp/m5stack_core_s3/include/bsp/m5stack_core_s3.h) | `63b9b375b6405470002fc03f3518204312076bfd0acefb190b76e12d175c6e38` |
| `M5Unified.cpp` | [下载](https://raw.githubusercontent.com/m5stack/M5Unified/master/src/M5Unified.cpp) | `fc6dd5141febc3f04e6a20ada9fee2b2287b07701e5495c5bf52ca83e9375146` |
| `M5Unified-LICENSE` | [下载](https://raw.githubusercontent.com/m5stack/M5Unified/master/LICENSE) | `44611d917bece311f5c8eeffce83d8bf9e9ca3f815d262f694ff4a7996af50ea` |
| `CoreS3-Lite.html` | [下载](https://docs.m5stack.com/zh_CN/core/CoreS3-Lite) | `a1b392faa3e1619398c2613a9f29d220a1054286ecb9c38fa4dc6cf21ff93a8c` |
| `Sch_M5_CoreS3_v1.0.pdf` | [下载](https://m5stack-doc.oss-cn-shenzhen.aliyuncs.com/490/Sch_M5_CoreS3_v1.0.pdf) | `58a15454eccb11d2668e1a9a3ad85943b9a58b104c5f1ed137b790192ec27c04` |
| `AXP2101.pdf` | [下载](https://m5stack.oss-cn-shenzhen.aliyuncs.com/resource/docs/datasheet/core/K128%20CoreS3/AXP2101_Datasheet_V1.0_en.pdf) | `421011983c56034e1074d28e22826e8414d55f8dc5344cd67967942e3bc8b8d9` |
| `ES7210.pdf` | [下载](https://m5stack.oss-cn-shenzhen.aliyuncs.com/resource/docs/datasheet/core/K128%20CoreS3/ES7210.PDF) | `909a13dcd8770835914ef0de0cb5a44aba6d4312daf1d99768e0dab65f0e87e6` |
| `AW88298.pdf` | [下载](https://m5stack.oss-cn-shenzhen.aliyuncs.com/resource/docs/datasheet/core/K128%20CoreS3/AW88298.PDF) | `d9060b2d8a19f4bcb85c5e116458dfd53973d9ddfd26b334ff8b149be064a9bf` |
| `GC0308.pdf` | [下载](https://m5stack.oss-cn-shenzhen.aliyuncs.com/resource/docs/datasheet/core/K128%20CoreS3/GC0308.PDF) | `b8a73e544473cd14e04760db5c0c906129036ef0d2f5f86d87f451d3df5f4dcd` |
| `AW9523B.pdf` | [下载](https://m5stack.oss-cn-shenzhen.aliyuncs.com/resource/docs/products/core/CoreS3/AW9523B-CN.pdf) | `24022acd5860b74eb898b08a93e09287bde1909fae3caab4eb27a8a2a1b12d42` |
| `ILI9342E.pdf` | [下载](https://m5stack-doc.oss-cn-shenzhen.aliyuncs.com/644/ILI9342E.pdf) | `2bcca2655e73d4b569583e518dc64b514678dea6ccb8b3ed8bfcf081bdc0c188` |
| `Panel_ILI9342.hpp` | [下载](https://raw.githubusercontent.com/m5stack/M5GFX/master/src/lgfx/v1/panel/Panel_ILI9342.hpp) | `675e3837c13306b72826c9d3a8e69b3d7dd680e334e50784cbbea2c616b79a16` |
| `M5GFX.cpp` | [下载](https://raw.githubusercontent.com/m5stack/M5GFX/master/src/M5GFX.cpp) | `314251ee69846afa754e11738bb44eaf00509c2b8b5b9de679f34e10411af6bc` |

用户原有 `icoss1060691881e8496f5b7b0a85cb.pdf` 是 2026-03-02 的产品文档快照，
用于比对旧屏说明；其内容没有包含 2026-08-07 的 ILI9342E 变更。原有
`Sch_M5_CoreS3_v1.0.pdf` 与当前官方链接对应。对外说明以官方页面和明确板型为准。

## 关键结论与证据等级

- **原理图确认**：第 3 页 U7 为外置 PSRAM，只有 SIO0～SIO3 四条数据线，
  连接 FLASH_D/Q/WP/HD，片选 FLASH_CS1；必须使用 Quad 模式，不能从“8 MB”
  或 ESP32-S3 型号推断为 Octal。80 MHz 配置同时参照旧 SE 工程，仍待本板启动与内存测试。
- **来源交叉确认**：I²C SDA12/SCL11；AW88298 输出 GPIO13、ES7210 输入 GPIO14。
  官网音频表把 13/14 的方向写反，原理图第 2、3 页与 Espressif BSP 均支持 RX14/TX13。
- **来源交叉确认**：GC0308 数据位为 39/40/41/42/15/16/48/47，PCLK45、VSYNC46、
  HREF38。板上 24 MHz 振荡器提供摄像头时钟，不能套用立创的 GPIO5 XCLK。
- **来源交叉确认**：AXP2101 管电源和背光；AW9523B 管 LCD/触摸/摄像头/功放复位。
  GPIO0 是音频 MCLK，不能作为立创 BOOT 业务按键。电源键和 RST 保留硬件用途，业务使用触摸。
- **来源交叉确认，待实测**：ES7210 TDM 模式输出 MIC1/MIC3/MIC2/MIC4，MIC3 接
  扬声器差分回采。适配器保留四槽读取，再压紧前两槽到公共 AEC 的 MR 输入。
- **明确变体，待实测**：ILI9342C 与 ILI9342E 使用不同扩展命令及伽马表；320×240
  原生横屏、触摸不交换坐标。`ili9342e` 为默认，旧屏选择 `ili9342c`，不依赖日期猜实物。
  官方 `bsp_display.c` 和旧 SE 工程均将面板及 LVGL 的 swap/mirror 设为 false；
  共享 UI 注册必须采用本板方向，不能沿用立创原生 240×320 屏的交换 XY / 水平镜像配置。
  2026-10-20 用户指定摄像头在顶部使用：在原生横屏基础上同时镜像 X/Y，实现
  180° 旋转，swap 仍为 false。显示与触摸共同采用该板级参数；这项方向来自用户要求。
- **待实测**：板卡 PCB 丝印、屏幕型号、声音/双讲、摄像头方向、整机内存和温升。
  原理图、寄存器配置和编译通过都不能代替这些验收。

## 参考旧工程的内容与原因

用户指定的 `m5stack-cores3-se-xiaotai` 是技术参考，不是新的产品基线。
其目录名含 SE，但当前源码已有 Lite 摄像头供电和 H5 视频接入痕迹；采用前逐项核对。

| 参考实现 | 采用内容 | 原因与边界 |
|---|---|---|
| `components/starter_board/starter_board.c` | AXP2101/AW9523 初始化、LCD 复位、背光读改写、摄像头供电 | 与 Lite 原理图和官方 BSP 一致；保留 USB device 方向，避免覆盖其他电源位 |
| `sdkconfig.defaults` | `SPIRAM_MODE_QUAD`、80 MHz PSRAM 配置 | Quad 与 Lite 原理图 U7 四线接法一致；修正从立创带入的 Octal 配置，不能继承 SE 的实板验证结果 |
| `components/starter_media/src/starter_media.c` | ES7210/AW88298 codec 配置，共享 I2S 64fs 的 AW88298 寄存器读回 | 两个音频器件相同；4×16-bit RX 与 2×32-bit TX 共享 BCLK，不能照搬 ES8311 |
| `docs/音频与资源.md` | MIC1/MIC3/MIC2/MIC4 槽序与回采归属 | 与 ES7210 数据手册、原理图交叉核验；本次仍需实板验证槽位和 AEC |
| `components/starter_product/src/starter_product.c` | ILI9342C 面板运输层、FT6336 原生横屏坐标 | 只移植硬件初始化；ILI9342E 表另取当前官方 M5GFX |

没有移植旧工程的业务状态机、页面、抖动缓冲、AGC、额外 ISR 采集队列或旧依赖版本。
原因是本仓库的产品运行时、公共 AEC、20 ms 音频链路、UI 和测试才是当前探鸽小钛基线。
采集用 `esp_codec_dev_read` 保持单一 I2S 消费者，避免为参考工程的诊断队列引入第二套所有权。
旧工程的历史实板结论不用于本次验收。

本仓库 S3 媒体源码移到 `platforms/esp-idf/components/starter_media_s3/` 共用，立创原路径
保留兼容链接。音频、摄像头使用已有稳定 C adapter；旧 `szpi_board_*` 调用名在 M5 头文件中
映射为本板符号，避免复制产品逻辑。配网、绑定、会话仲裁、AI、呼叫、房间和 UI 均直接
编译当前公共实现。默认 SDK 为 TiRTC 2.5.0，当前应用工具链为 ESP-IDF **v5.5.5**。

## 旧工程参考文件身份

- `components/starter_board/starter_board.c`：SHA-256 `e76791b92683be12a1ca39e5e2350d6b41ba5e673cb707b4f353be9dd2d31903`。
- `components/starter_media/src/starter_media.c`：SHA-256 `fe69e6a52602e5866e3aa6e2d2a5bf7ff8da71141df468cb863eeb6ed0f4a10d`。
- `components/starter_product/src/starter_product.c`：SHA-256 `33a0f98723db05a18cd92840de79073540c77f13d8262bb0f4ba31a3f9f3e1f8`。
- `docs/音频与资源.md`：SHA-256 `d331cd65e5abeb9ac2da3b50341557ab23c4af45d89962e14947f91e107e6355`。

- `sdkconfig.defaults`：SHA-256 `449d51b82d6f8b416f920a700c5d3112045d2cde378bd8ee364721adfa836127`（Quad PSRAM / 80 MHz 配置参考）。

## build.10 自回声反馈后的专项比较

用户报告 AI 偶尔把自己的回复当作用户说话。当前用 ESP-IDF v5.5.5；
这次比较保留本仓库 Opus/16 kHz AI 合同，未将旧工程的音频实现直接覆盖进来。

| 边界 | 当前 Lite 实现 | 参考工程 | 结论 |
|---|---|---|---|
| 模拟增益 | 初始化 MIC1/2 请求 30 dB；默认档 4 请求 MIC1 为 33 dB，但锁定驱动将其量化成 30 dB；MIC3 请求 0 dB | MIC1/2 为 24 dB；MIC3 为 0 dB | 日志为请求值，不能当作寄存器实测；档 3/4 在当前驱动下相同。后续官方核查见下文 |
| 消回声 | ESP-SR 2.4.7 直接 `aec_process`，MR、FD_LOW_COST、长度 4、AGGR | ESP-SR 2.5.0 AFE，MR、相同 AEC/NLP 档位，另有 VAD | 两者均有非线性抑制，不能将直接 AEC API 等同于“少了 NLP”；暂不升级算法 |
| 输出增益 | 当前公共输出，无参考工程的额外 AGC 链 | AFE 后约 +6 dB、独立 AGC、发送前限幅 | 先排除 AEC 前削顶；直接加 AGC 也可能放大残余回声 |
| 采集与计算 | 同一任务读 I2S 并运行 AEC；编码另有有界队列 | 采集、AFE feed/fetch 分离，并统计采集丢块 | 有参考价值，但需要连贯性、丢块和内存证据；不能仅凭超时计数复制额外队列 |
| 编译与缓存 | Debug 优化、D-cache 32 KiB / 32 B line、I-cache 16 KiB | 性能优化、D-cache 64 KiB / 64 B line、I-cache 32 KiB | 可能影响耗时；扩大缓存会改变内部 RAM 预算，须单独构建及实测，不与增益调整混测 |

`components/starter_media/src/starter_aec.h` 的
`STARTER_AEC_MIC_PGA_DB=24` 与 `STARTER_AEC_OUTPUT_GAIN=1.9952623`
解释了参考工程的增益分配。其 AEC 实现 SHA-256 为
`94b70d6cd6a12cab839badc31eddee4c344d8a8c4018ef71dabcc464c785a709`，
头文件为 `1f8afa2eaccbeac5a6c1c821f116e23d0c6a466d1119ed2ad7055854a962a6f5`。

乐鑫 [麦克风设计指南](https://docs.espressif.com/projects/esp-sr/en/latest/esp32s3/audio_front_end/Espressif_Microphone_Design_Guidelines.html)
要求回采不要饱和，并给出功放失真与 ADC 输入范围约束。HTML 已保存到
`.references/M5Stack_CoreS3_Lite/Espressif_Microphone_Design_Guidelines.html`，
SHA-256 `36d84c0c44d05ca958edb7d4f1993bde47cd1b1cbeb8204ae32447041807fa6f`。
这支持先核对输入余量，不能证明本次自回声已定位或解决。

此前提出的同版对照为：扬声器音量、位置和网络不变，从灵敏度 4 改到 2；
远端独自播放时保持安静并执行 `voice-diag 10`，前后各取一次 `status`。
比较削顶、处理超时的增量，以及播放中 MIC/ref/clean 电平；再测试近端单讲和双讲。
停播时参考接近零是正常可能情况，不能用那一窗口判断回采断路。

用户随后要求先查官方实现，暂停上述参数试调。官方核查找到当前板级
AW88298 初始化遗漏 `.hw_gain.pa_gain = 15`，已按乐鑫 CoreS3 BSP 补齐；
这是音量映射的硬件增益补偿，不是修改 AEC 或直接提高功放增益。
固定源码提交、驱动量化复现、采用依据与未解决事项见
[官方音频核查](OFFICIAL_AUDIO_REVIEW.md)。旧 SE 工程的 AFE、AGC、缓存与任务配置
没有因本次核查被复制进来。
