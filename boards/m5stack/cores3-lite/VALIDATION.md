# CoreS3-Lite 验证记录

工具链：ESP-IDF **v5.5.5**，提交 `b774170ff46c393eeb5e495ea37936038d3f4f4f`。
板卡 K128-Lite 的物理 PCB 和屏幕版本未确认。用户 build.9 日志确认 PSRAM 与应用启动，
照片显示花屏；build.10 已由用户运行并报告触摸暂时失去响应、自回声问题。
当前 build.14 补齐摄像头 180° 上报与官方 AW88298 PA 增益补偿，尚未实板验收。本次未由代理烧录、未发布。
交付模式为本地开发构建，不创建 GitHub Release 或发布包。

| 检查 | 结果 |
|---|---|
| Skill 0.10.1 Doctor：目标编译器、ESP-IDF、TiRTC ABI | PASS；未使用旧 managed kit |
| Hardware IR v2 校验、严格 intake | PASS / READY_TO_PORT；不是实板证明 |
| 新增公共边界回归 | PASS，7 项板级回归 + 5 项 PSRAM 配置回归 + 3 项显示/触摸几何回归；先失败再修正 |
| `python3 tools/build.py --validate` | PASS，5 块板 |
| `python3 -m unittest discover -s tools/tests -v` | PASS，378 项 |
| `bash tools/check.sh` | PASS；产品核心行覆盖率 93.09%、分支覆盖率 63.74% |
| `ili9342c` | build.6 仅历史编译通过，含错误 Octal 配置，禁止继续使用；本次未重建 C 变体 |
| `ili9342e` 原生构建 | PASS，`1.0.1+build.14`，Quad PSRAM / 180° 显示、触摸与摄像头上报 / 官方 PA 补偿，退出码 0 |
| PSRAM 有效配置与最终链接 | PASS，`CONFIG_SPIRAM_MODE_QUAD=y`；ELF map 链接 `esp_psram_impl_ap_quad.c.obj` |
| BIN/ELF 身份、镜像内板名、单一 I²C 驱动族 | PASS |
| 编译输入隔离 | PASS，只编译 M5 板级源码；C 屏定义仅作用于显示组件 |
| 旧固件启动 | FAIL，用户日志：`octal_psram` 初始化失败，在 `mspi_init` abort 并循环复位 |
| build.9 启动 / 内存测试 | PASS（用户日志）；8 MB / 80 MHz，内存测试通过并进入配网；不代表已绑定上线 |
| build.9 显示 | FAIL（用户照片），共享 LVGL 配置覆盖板级方向 |
| build.10 绑定与 AI | 用户日志确认凭证保存、MQTT 在线、AI 准入和上下行计数；声学未通过 |
| build.10 交互与 AEC | FAIL（用户反馈）：触摸暂时无响应，随后恢复；偶发把自身回复识别为用户说话 |
| build.12 AI 双讲 | FAIL（用户反馈及日志）：安静时能识别，播放时难以打断；有输入削顶和处理超期 |
| build.14 显示、触摸、媒体、H5、AI、设备/微信通话、PTT、双讲、稳定性 | SKIP，待实板 |

当前产物属于 **COMPILE_PASS**。该仓库使用共享源码和当前 Opus AI 合同，通过仓库的
行为回归及原生构建门禁验证；未套用独立生成工程的旧 A-law AI 静态合同，也未声称通过
该 Skill 的独立 BUILD_VERIFIED 认证。设备业务与声学可用性仍须实板验收。

## 当前保留产物：ILI9342E

- 应用：`firmware/esp-idf/esp32s3/m5stack-cores3-lite/build/m5stack_cores3_lite_xiaotai.bin`
- 版本：`1.0.1+build.14`；大小：4097904 字节。
- 应用 BIN SHA-256：`ad624368092a620726ca9a3c4554e79aefba608da1ba5843157c24e34792d6e9`。
- ELF：同目录 `m5stack_cores3_lite_xiaotai.elf`；大小：29137896 字节。
- 完整 ELF SHA-256：`3f97c9b8f0589a5164ed2e4cce40714c17e8b6a43b825be963a07b34f6fef396`；与 BIN 内 `app_elf_sha256` 一致。
- 原生引导与分区产物、烧录地址见同目录 `flasher_args.json`。BIN 不是从 0 地址烧录的合并镜像。

## 旧产物与失败证据

旧 build.6 / build.7 保留了立创的 Octal 配置，不适用于原理图 U7 的 Quad PSRAM。
用户日志显示 IDF v5.5.5、`Failed to init external RAM`、`mspi_init` abort 和
`RTC_SW_CPU_RST`；日志未提供应用 SHA-256，不把它冒充已校验的同版 HIL 记录。
修正默认值后，使用原本地 Octal sdkconfig 执行 `idf.py reconfigure`，确认新 CMake
检查拒绝错误配置；再仅修改本地模式，保留其他设置，完成 Quad 构建。
build.8 在沙箱中完成镜像生成但构建进程未正常退出；重新构建分配 build.9 并退出 0。

### build.9 用户启动与花屏记录

用户日志显示版本 `1.0.1+build.9`、编译时间 22:53:10、ELF 前缀 `2871d1430`，
与本地构建一致。8 MB PSRAM、80 MHz、`SPI SRAM memory test OK`、GC0308 识别、
UI 初始化和配网热点启动均已输出；未验证麦克风/扬声器音质、绑定上线或通话。
用户照片仍有文字错行和大块空白。设备日志只含 ELF 前缀，未读取完整设备镜像校验值。

对应本地旧应用 BIN SHA-256：`62b080406fdc96c46d7395f4d8a7063214c57900aae3b60808ec551d5a690370`；
完整 ELF SHA-256：`2871d143069e7ea9cdb9d9367dd53b1873983ea48bc0f9137894c9d1c7b313e7`。
当前构建已覆盖旧产物，这些启动观察不转移为新版本的硬件验收。

生产 `lvgl_init()` 回放证实：强制交换 XY 将 ILI9342 的地址范围改成 240×320，
与 UI 320×240 不符。build.10 的板级、LVGL 注册和触摸共用方向参数；当时 CoreS3 均为 false，
立创 ST7789 保留原来的交换 XY / 水平镜像。仍待实板验证花屏消失及四角触摸。

### build.10 用户在线运行与声学反馈

用户 `status` 显示 `1.0.1+build.10`、22:59:20、ELF 前缀 `c7a9d935a`，
与本地旧产物身份一致；完整设备镜像未读回。对应本地旧应用 BIN SHA-256：
`9de14af2805134798969a84dad0531535f9aabdba0a6e1f8c5c8a86d3186936d`；
完整 ELF SHA-256：`c7a9d935a0d47c23f31a8159c3e755a63d99f6847a78ec1730f9fe8f6640f78d`。

触摸失去响应时串口、UI 定时告警仍工作；AI 保持 active，接收/解码/播放均为
5362 帧，dropped、rx-overflow、decode-fail、i2s-fail 均为 0。随后用户报告恢复，
未取得触摸原始坐标与事件计数，不能归因为 I²C、绘图或调度中的某一个环节。
`UI refresh slow` 的 20–80 ms 是墙钟耗时且按 5 秒限频，不代表固定周期卡死。

AEC 处理 9101 块、超过 32 ms 的处理计数 2240，最大 111861 us；MIC/ref
削顶样本累计 351474/0。采集流程含持续 AEC，超时可能影响连续性，但当前没有
同窗口 I²S 丢块证据。最近诊断窗口参考峰值仅 6，且播放已停止，不能用此证明回采断路。
用户明确报告偶发自回声，尚缺播放中电平和近端/远端/双讲对照；见 SOURCES.md。

### build.11 绑定入口修复

错误来自共享 `platforms/esp-idf/main/Kconfig.projbuild` 的历史默认值，
Git blame 指向 `e17e7b40`（2026-10-01）；CoreS3 saved sdkconfig 继承它。
串口改用产品层地址，屏幕同源；删除只影响显示的旧配置，保留服务发现/API 配置。
`test_onboarding_faults.py` 先以旧 sdkconfig 复现错误输出，再验证修复后始终提示
`https://xiaotai.chat`，成功和失败重试都覆盖；原有绑定 UI 回放继续通过。
本次没有修改 AEC、增益、缓存、任务优先级或消除 UI 慢刷告警。

### build.12 摄像头在顶部的显示方向

用户要求在 build.11 原生横屏基础上旋转 180°。仅 M5 板级配置改为
swap=false、mirror_x=true、mirror_y=true，C/E 两个屏幕变体共用。
面板初始化、LVGL 注册、触摸均读取同一参数；不修改摄像头编码画面或其他板卡方向。

`test_s3_display_geometry.py` 在旧方向复现显示/触摸两个失败，修正后 3 项通过，
包括立创方向不变。触摸按 esp_lcd_touch 1.2.1 的 `max - raw` 软件镜像语义设置
包含端点的 319/239，验证四角及中心不越界；显示仍保持 320×240，无软件旋转缓冲。
待用户实测摄像头朝上时的显示和四角按钮，当前未烧录。

### build.13 摄像头方向上报

生产 HTTP 提交回放在修正前复现 M5 profile 缺少 `camera_rotation`、设备呼叫两入口
沿用 270°。M5 runtime component 现在单独定义 `XIAOTAI_BOARD_CAMERA_ROTATION=180`，
用于 stream/call/voip profile 和呼叫 request/device-info 的数值字段。
摄像头像素、编码尺寸及音频通话能力不变；立创与 P4 的原字段和值由同一测试验证。
`test_camera_rotation_report.py` 3 项、全量 377 项 tools unittest、`tools/check.sh`
均通过；原生 E 变体编译退出 0，ESP-IDF v5.5.5，应用描述符与完整 ELF 哈希一致。
尚未烧录；升级联网并重新打开实时查看后，需用有上下左右文字的画面验收。

### build.12 播放时难以打断：证据范围

用户日志的版本、编译时间与 ELF 前缀 `e278d1e71` 匹配历史 build.12；完整设备镜像
未重新读取。该版本本地 BIN/ELF 已被 build.13 替换，不能把旧观察作为新版本通过证据。
用户确认安静时可识别，扬声器播放时说话难以打断。10 秒观察窗口内，麦克风峰值
反复为 32768，回采 RMS 145–384，AEC 输出 RMS 1040–2315；回采有数据但不能据此
证明槽序、同步或双讲正确。会话 status：706 个 AEC 块、18508 个麦克风削顶采样点、
617 次处理超期，播放中平均处理约 45–47 ms（每块 512 点 / 16 kHz = 32 ms）。
这些为墙钟处理耗时，包含任务被抢占的时间，不等于 AEC 算法独占 CPU 时间。
最后 status 的回采接近零时播放已经停止，不能与播放中的窗口混用。

立创 S3 与 M5 共用相同 AEC/Opus/AI 上行源码，且 `libesp_audio_processor.a`
SHA-256 均为 `670639c3d136c7e3909aa90bcaac573600b7e98b6850be8fd5484b4cdea96dcc`。
两者均为 FD_LOW_COST + AGGR，16 kHz MR，单块 512 点；M5 读取四槽后压紧，
立创由 codec 层选择两个 DMA 槽。两板都没有“扬声器播放时关闭 AI 上行”的分支。
M5 Quad PSRAM 与立创 Octal PSRAM 不同，尚不能从总耗时直接判定带宽为根因。
此次诊断先核查官方资料；未更改 MIC 增益、AEC 模式、缓存或任务优先级。后续只按明确官方依据补齐 PA 补偿，见下节。

### build.14 官方功放标定修正

核对 Espressif 官方 CoreS3 BSP 固定提交后，确认 build.12/13 的 AW88298 配置漏了
`.hw_gain.pa_gain=15`。真实 `audio_codecs_init()` 边界测试先因 PA gain=0 失败，
补齐板级配置后通过；仍验证 MIC1 初始化请求 30 dB、MIC3 请求 0 dB及共享时钟初始化。
独立重放官方音量函数证实，它改变的是 DAC 衰减补偿，而不是增加实际功放增益。
此修正只编译进 M5 板卡，不改变立创 S3/P4 的硬件或音频代码。

编译版本 `1.0.1+build.14`，ESP-IDF v5.5.5，2026-10-21 00:02:37，退出 0。
全量仓库检查与 378 项 tools 测试通过；源码证据、请求 33 dB 实际量化为 30 dB 的
复现、官方普通录音示例的适用限制见 [OFFICIAL_AUDIO_REVIEW.md](OFFICIAL_AUDIO_REVIEW.md)。
build.14 尚未烧录，不能声称已解决削顶、45–47 ms 处理超期或自然打断。

### ILI9342C 历史构建记录（不可继续使用）

`1.0.1+build.6` 应用 BIN SHA-256 为 `c8dc81297e2bab27cbde55043919a27d8176b3850c0b49628dc1733112a39720`，
完整 ELF SHA-256 为 `abecd3302f3251af5b797d17cb86fc4f9ec84fe6d3d81861ef4334b0fd0feacd`。
该次原生产物随后由 E 变体构建替换；如需 C 屏，按开发板指南重新构建并核对新编号，
不要把当前 E 屏 BIN 当成 C 屏固件。无同版实板测试记录。

## 仍需硬件确认

确认 PCB/屏幕实物；两版屏幕的颜色、背光和四角触摸；电池/USB 开关机；
摄像头方向；麦克风、扬声器和回采槽位；各业务的近端单讲、远端单讲、双讲与尾音；
配网/绑定/重置、重复通话、房间离页、网络恢复、运行内存和长时间稳定性。
实际行为必须绑定新产物 SHA-256，不能继承旧 SE 或立创板的通过结论。

## 对立创 S3 与微雪 P4 的影响

对照适配前提交 `c442832`：新板的 GPIO、电源、Quad PSRAM、屏幕 C/E 命令表、
AW88298 及 ES7210 配置只由 M5 工程编译。以下共享改动会随其他板卡重新构建生效，
不是对其他板卡“零改动”；原有线上固件不会因本地源码修改而变化。

| 改动 | 立创 S3 | 微雪 P4 |
|---|---|---|
| S3 媒体源码移入平台目录、原路径保留链接 | JPEG collector 逐字节一致；媒体主文件仅首段注释变化，AEC/Opus/队列/任务逻辑未改 | 不使用该 S3 媒体源码 |
| 板级显示方向宏 | 仍为 swap=true、mirror_x=true、mirror_y=false，回放生产 LVGL 注册验证兼容 | 该初始化位于非 P4 条件分支，P4 显示不变 |
| 上报型号宏 | 缺省仍为 lckfb-esp32s3 | 保留原 P4 profile 分支；M5 覆盖只在 M5 component 定义 |
| 绑定入口共用产品常量 | 串口旧演示站修正为 xiaotai.chat | 保持原本正确的网址，移除重复显示配置；服务发现未改 |
| 构建分发与版本 | 单变体仍执行 idf.py build；本地验证构建号按规则递增 | 同左 |
| ESP-IDF 版本说明 | 补明当前 v5.5.5，没有在此次适配中升级工具链 | 同左 |

共享 S3 媒体层未来的行为修改会同时影响立创与 M5，必须继续执行两板回归。
共享产品文案还被 BK 使用，本次展开后的屏幕文字保持不变，已有绑定渲染回放覆盖。
原生编译与主机测试不能代替两板的显示、音频及通信实板复测。

本分支在 ESP-IDF v5.5.5 下依次使用 `tools/build.py` 完成两板原生构建，退出码均为
0；两板版本均为 `1.0.1+build.3`，各自 BUILD_NUMBER 从 2 递增到 3。无烧录。
P4 构建后的依赖锁已由分发器还原为可移植路径，组件版本没有变化。

| 目标 | 应用 BIN SHA-256 | 完整 ELF SHA-256 |
|---|---|---|
| 立创 S3 | `2206dcfd23e4166987354c82791b700be5bab51af8c88ddeaebc2ecd5ee1bb13` | `122276fe2a14cf2a4895ecfa509a2d1b63d1f88cf45c04db11c585a6da8566b0` |
| 微雪 P4 | `1b32c75888bca9daa8179ed7203aa744d752487ef0124f0f4dc1729767c01737` | `6a0c8c87401054b288ad2ab0b2a6a008fafde5075775106e21fef1fde2ef0b34` |

主机证据：完整 `tools/check.sh`、374 项 tools unittest、5 块板 manifest 校验，
以及两板应用描述符版本/完整 ELF SHA-256 核验。显示方向、上报身份与绑定提示
分别由 `test_s3_display_geometry.py`、`test_cores3_board.py`、
`test_voip_profile.py`、`test_onboarding_faults.py` 覆盖。
