# CoreS3-Lite 验证记录

最近实板测试版本为 `1.0.1+build.25`（ESP-IDF v5.5.5 / ILI9342E），已授权烧录并完成基础观察。
只将 M5 的 2048+2304 字节字幕数组移入外部 BSS。原生 ELF 相比 build.24：
内部 BSS 82104 → 77752 字节，外部 BSS 32300 → 36652 字节，内部 data 不变。
有效 sdkconfig 与 build.24 完全一致，保留其 64 B 缓存行候选；设备此前回刷的
build.23 使用 32 B 缓存行，不能把 23→25 的全部变化归因于字幕迁移。

新主机回归先失败再通过，执行各板真实组件选择及编译段检查，覆盖 M5 C/E 变体、
立创/P4 隔离、错误配置拒绝、容量和历史文本读写。原生构建与镜像身份核验通过。
精确产物保留于工程 `artifacts/1.0.1+build.25/`，哈希见 `hardware-ir.json`。
启动、PSRAM 测试及保留配网/绑定后的联网正常。六轮 AI 建连/挂断均成功，恢复唤醒；
静置 155 秒，内部空闲恢复并稳定在 53895 字节，最大连续块 29696 字节（29 KiB）。
烧录前 build.23 对应数值为 49519 / 24576；本次短测未显示持续内存流失。

播放诊断采到 61 个持续播放窗口：平均 wall/audio=1086.95/1024 ms，AEC 包装平均
31.87 ms；观察到 1 次采集溢出，实时性目标仍未通过。首次长播放下行收到/解码/写入
均为 10157，解码/I2S 错误及 RX overflow 为 0；一轮重连的媒体 dropped=1，原因未定位。
用户随后反馈 build.25 第 8 轮难打断，停止播放后也经常听不清，收音/声学验收未通过。
字幕目视、触摸尚未获人工确认；设置保存/解绑及其他业务未在本轮执行。
本机明细与原始证据位于 `tests/.local/cores3-psram-text/HIL.md`。

工具链：ESP-IDF **v5.5.5**，提交 `b774170ff46c393eeb5e495ea37936038d3f4f4f`。
板卡 K128-Lite 的物理 PCB 和屏幕版本未确认。用户 build.9 日志确认 PSRAM 与应用启动，
照片显示花屏；build.10 已由用户运行并报告触摸暂时失去响应、自回声问题。
历史 build.18 已使用统一 AI 合同：上行 A-law / 8 kHz，下行 Opus / 16 kHz。
用户 build.17 日志已确认协商成功和双向媒体收发；新版本声学验收仍待完成。
build.15 已由用户运行，诊断证实播放期间存在 I2S 采集队列溢出。
build.14 用户已运行，但报告每轮经常需要重复。此次未由代理烧录、未发布。
本次交付为本地封版验证包，不创建 GitHub Release 或替换线上固件。

| 检查 | 结果 |
|---|---|
| Skill 0.10.1 Doctor：目标编译器、ESP-IDF、TiRTC ABI | PASS；未使用旧 managed kit |
| Hardware IR v2 校验、严格 intake | PASS / READY_TO_PORT；不是实板证明 |
| 新增公共边界回归 | PASS，7 项板级回归 + 5 项 PSRAM 配置回归 + 3 项显示/触摸几何回归；先失败再修正 |
| `python3 tools/build.py --validate` | PASS，5 块板 |
| `python3 -m unittest discover -s tools/tests -v` | 历史 PASS，391 项；封版重跑结果见下方 |
| `bash tools/check.sh` | PASS；产品核心行覆盖率 93.10%、分支覆盖率 64.48% |
| `ili9342c` | build.6 仅历史编译通过，含错误 Octal 配置，禁止继续使用；本次未重建 C 变体 |
| `ili9342e` 原生构建 | PASS，`1.0.1+build.18`，保持 Quad PSRAM、180° 方向和官方 PA 补偿；统一 AI 非对称格式，退出码 0 |
| PSRAM 有效配置与最终链接 | PASS，`CONFIG_SPIRAM_MODE_QUAD=y`；ELF map 链接 `esp_psram_impl_ap_quad.c.obj` |
| BIN/ELF 身份、镜像内板名、单一 I²C 驱动族 | PASS |
| 编译输入隔离 | PASS，只编译 M5 板级源码；C 屏定义仅作用于显示组件 |
| 旧固件启动 | FAIL，用户日志：`octal_psram` 初始化失败，在 `mspi_init` abort 并循环复位 |
| build.9 启动 / 内存测试 | PASS（用户日志）；8 MB / 80 MHz，内存测试通过并进入配网；不代表已绑定上线 |
| build.9 显示 | FAIL（用户照片），共享 LVGL 配置覆盖板级方向 |
| build.10 绑定与 AI | 用户日志确认凭证保存、MQTT 在线、AI 准入和上下行计数；声学未通过 |
| build.10 交互与 AEC | FAIL（用户反馈）：触摸暂时无响应，随后恢复；偶发把自身回复识别为用户说话 |
| build.12 AI 双讲 | FAIL（用户反馈及日志）：安静时能识别，播放时难以打断；有输入削顶和处理超期 |
| build.14 AI 声学 | FAIL（用户反馈及同版身份日志）：每轮常需重复；有满量程输入及处理超期 |
| build.15 AI 诊断 | 用户日志证实播放期 RX 队列溢出，AI 重复说话问题未通过 |
| build.16 协商反馈 | FAIL，用户报告本地 unsupported audio profile；片段无完整身份/返回字段 |
| build.17 协商、媒体收发 | 用户日志确认 alaw/8000 输入、opus/16000 输出被准入，双向计数增加；详细声学未验收 |
| build.18 H5、AI、设备/微信通话、PTT、双讲、稳定性 | SKIP，待实板 |

当前产物属于 **COMPILE_PASS**。该仓库使用共享源码及四板统一 A-law 上行 / Opus 下行合同，通过仓库的
行为回归及原生构建门禁验证；未套用独立生成工程的旧 A-law AI 静态合同，也未声称通过
该 Skill 的独立 BUILD_VERIFIED 认证。设备业务与声学可用性仍须实板验收。

## 当前保留产物：ILI9342E build.28

本次 S3 纯音频策略原生构建退出 0；新镜像未烧录。两板能力上报、呼叫、媒体、SDK
与 UI 的公共入口回归通过；H5 视频保留，P4 通话策略不变。
精确版本为 `1.0.1+build.28`，ESP-IDF v5.5.5，屏幕变体 `ili9342e`。

- 应用：`firmware/esp-idf/esp32s3/m5stack-cores3-lite/artifacts/1.0.1+build.28/m5stack_cores3_lite_xiaotai.bin`，4020704 字节。
- 应用 BIN SHA-256：`14fc9e01b1254efd12260aa43fb0ae6119b52465fb7c8c9f6cab3a50b7245dcd`。
- ELF：同目录 `m5stack_cores3_lite_xiaotai.elf`，29081204 字节。
- 完整 ELF SHA-256：`cf921d580ba83d9dba3fced2e53390518bb406b343e5ffde37442b1b3cb19855`；与 BIN 内 `app_elf_sha256` 一致。
- 同目录保留 bootloader、分区表及原生 `flasher_args.json`；封版说明见
  [FREEZE.md](../../../docs/boards/m5stack-cores3-lite/FREEZE.md)。

分文件仅烧录清单中的镜像，合并 BIN 从地址 0 写入且覆盖跨度内的 NVS。
当前镜像不复用 build.25 的实板验收结论；以下 build.18 及更早条目为历史记录。

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
build.14 随后由用户烧录并提供同版日志，声学问题仍存在，见下节；不能声称 PA 补偿已解决双讲。

### build.14 持续对话失败记录及原样保留产物

用户 `voice-diag 30` 身份为 `1.0.1+build.14`、Oct 21 2026 00:02:37、
ELF 前缀 `3f97c9b8f`，与保留产物一致；没有完整设备 Flash 读回。
用户反馈每轮经常需要重复。窗口 215→216 的读取完成时刻从 224684 到
226231 ms，相隔 1547 ms，而 32 块仅含 1024 ms 音频。
多个有回采窗口出现 MIC peak=32768，AEC 墙钟均值 44–47 ms；无回采的部分
窗口 mic/clean RMS 为 891/799、797/876。日志没有同期播放标记和驱动溢出计数，
不能区分近端说话/扬声器回声，也不能由电平比直接认定 AEC 误删人声。

在重建前原样保留应用 BIN 与匹配 ELF（非重新编译）：
`firmware/esp-idf/esp32s3/m5stack-cores3-lite/artifacts/1.0.1+build.14/`，
文件名分别为 `m5stack_cores3_lite_xiaotai.bin` / `.elf`，附 `identity.json`。
BIN 大小 4097904，SHA-256：
`ad624368092a620726ca9a3c4554e79aefba608da1ba5843157c24e34792d6e9`。
ELF 大小 29137896，SHA-256：
`3f97c9b8f0589a5164ed2e4cce40714c17e8b6a43b825be963a07b34f6fef396`。

### build.15 只读关联诊断

ESP-IDF **v5.5.5**，编译时间 2026-10-21 10:40:32，原生构建退出 0。
采音读取前后取快照，随原有 32 块统计发布播放 worker、估算 DMA 尾音、
待播峰值、近满量程计数和上行/下行/应用丢弃增量。CoreS3 使用官方 IDF
`on_recv_q_ovf` 增加独立的 RX 溢出事件计数，ISR 仅做有界标量更新。

`voice-diag 30` 首尾输出已有最多 8 条运行时事件，只含固定标签和时间，
不包含用户文本或音频。会话换代丢弃未完成观测，计数重置不会成为巨量增量。
主机回归先失败再通过；全量检查通过。该版本仅提供诊断，未证明实板丢音根因
或自然打断改善；按[采集步骤](../../../docs/development/MEDIA_PIPELINE_DIAGNOSTICS.md#cores3-持续对话采音观测)
进行一次正常对话并保留完整命令输出。

共享 S3 会增加只读窗口采样，立创 S3 未提供驱动探针时明确显示 unknown。
共享控制台在 P4 显示关联观测 unavailable；P4 音频处理未改。
未更改两板的增益、AEC 参数、DMA 容量或任务优先级，也未编译或烧录两板新固件。

### build.15 用户实测与原样保留产物

日志身份 `1.0.1+build.15`、Oct 21 2026 10:40:32、ELF 前缀 `120843b92`
与本地 BIN/ELF 一致。窗口 147：worker/dma=31/32、wall/audio-ms=1558/1024、
AEC 平均 47514 us、capture-overflow=10；窗口 136、137 分别溢出 8、7 次。
多个 worker/dma=0/0 的窗口约 1 秒处理 1024 ms 音频且溢出为零。
这证实播放期本地采集丢失，不能再用应用 dropped=0 推断无丢音。
末次 status 为 3317 块、2259 次处理超预算、近满量程 MIC 样本 2855；
下行收到/解码/写入均为 4160。该现象不直接证明唯一根因是 Opus 编码。

重建前保留到
`firmware/esp-idf/esp32s3/m5stack-cores3-lite/artifacts/1.0.1+build.15/`，
文件名为 `m5stack_cores3_lite_xiaotai.bin` / `.elf`，附 `identity.json`。
BIN 大小 4100128，SHA-256：
`1e2ca63909727002d4638ae07d2449617e190255d85d7769d0f91d380ecb1b9e`。
ELF 大小 29150636，SHA-256：
`120843b92998ba54c1628c81512836b8021a67b4906717d013e94a250d299157`。

### build.16 AI 上行 G.711A 对照

用户要求只改上行进行比较。ESP-IDF **v5.5.5**，2026-10-21 11:08:12，
版本 `1.0.1+build.16`，编译退出 0。M5 独有 Kconfig 选项
`CONFIG_STARTER_AI_UPLINK_G711A=y` 已在最终 sdkconfig/header 核验。
会话 input 为 g711a/8000/mono，output 保持 opus/16000/mono；响应按两个方向
分别校验。实际发送 stream 1、TIRTC_AUDIO_ALAW、8K16B1C，每 20 ms / 160 字节。

AEC 仍采集并处理 16 kHz，使用已有低通降采样的 clean8k，预录音按 8 kHz
与实时音频拼接，时间戳步长仍为 20 ms。保持 codec 初始化及缓冲分配，
仅跳过 AI 上行的 Opus reset/process，使用已有 G.711A 编码器。
没有同步调整增益、音量、AEC 参数、任务优先级或缓存容量。
默认构建的立创 S3、P4 继续使用 Opus 上行；未重建或烧录其他板。

新增 5 项生产边界回归，原有默认 Opus 回归继续通过，386 项 tools 测试与
`tools/check.sh` 通过。服务器是否接受非对称编码、编码耗时与采集溢出是否下降、
是否改善重复说话/自然打断，均待本版实板验证；这不是已验证的 AEC 修复。

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


### build.17 协议名称与响应诊断

用户在 build.16 交付后报告 start_session 被本地拒绝（262），没有进入媒体阶段。
片段未提供固件身份及完整格式响应，不能断言设备返回的具体 codec。
[官方 AI 文档](https://github.com/tangeai/tirtc-server-example/blob/fd70964a5ad3d7bd100c4241babb8f07d8d0c6fd/thing-connect/device-ai.md#2-发送-start_session)
规定规范名 `alaw`、兼容别名 `g711a`。此前只接受 `g711a` 是可复现的协议缺陷。

生产 start_session 和 CONNECTING 响应分支回放先出现两个失败：请求名不规范，
合法 alaw 响应无法准入；修正后六项 G.711 回归及四项默认 Opus 回归通过。
错误格式仍拒绝，新增白名单诊断不打印会话标识、未知文本或原始响应。
本次没有改变 AEC、音量、增益、调度、帧长及实际上下行编码。

build.17 编译时间 Oct 21 2026 11:28:19，ESP-IDF v5.5.5；BIN/ELF 身份一致。
当时未代理烧录；随后收到的 build.17 用户实测见下节。
此历史版本尚未推广至立创 S3/P4；当前统一合同已替代该默认值。

重建前已将 build.16 原 BIN/ELF 保存在工程 `artifacts/1.0.1+build.16/`，
对应应用 BIN SHA-256：
`8404be4ac8828c4955109315fee00aa9d86e5778781cb649101008f2dc8c7fe6`；
完整 ELF SHA-256：
`e42301189d67aac3a792d6a98faa231899de7886c5abc4f79e59da1ced581861`。
大小分别为 4016640 / 29064408 字节，不将此次反馈冒充完整镜像回读验收。


### build.17 用户实板日志与 build.18 统一合同

用户日志中的版本 `1.0.1+build.17`、编译时间 Oct 21 2026 11:28:19、ELF 前缀
`ad24d9071` 与重建前保留的 `artifacts/1.0.1+build.17/` 一致。该历史 BIN SHA-256
为 `f3e31adeabc3df82aaf5071ceb2a2129dc57ee00dce729edba862d6668f0d7fd`，完整 ELF SHA-256
为 `ad24d9071105f21e6c7ceb45f95492941b97d42ae8e22ed2cfaf7af9acf8c732`。
这是用户日志与本地产物身份匹配，不是闪存全量回读。

- `input=alaw/8000 output=opus/16000` 准入成功，首包 160 字节，预录音完成。
- TX 9355；RX、decoded、played 均为 1677；解码失败、I2S 写失败、RX 溢出均为 0。
- 编码平均 / 最大耗时 144 / 7751 µs；AEC 5816 次、错误 0，超期 539 次。
- 播放窗口 209：1043 ms 墙钟对应 1024 ms 音频；采集溢出 0，麦克风削顶 48 点。

旧 build.15 的播放窗口曾出现 1.5 秒处理 1.024 秒音频、每窗口 5–10 次采集溢出。
两次录音环境和对话内容不同，只能作为减轻编码负载的迹象，不能据此证明 AEC 完全修复。
当前仍需同一距离、音量和话术下的远端单讲、近端单讲、双讲插话、连续对话和长跑测试。

build.18 编译时间 Oct 21 2026 12:46:11；原生构建、配置与 BIN/ELF 身份检查通过。
当前共享合同也用于立创 S3 build.5、P4 build.5、BK7258 build.57，均完成原生构建。
P4/BK 保留 16 kHz AEC/NS/AGC，再低通降采样到 8 kHz 编码；没有更改板级增益、时钟或优先级。
P4 预录音尾段保留到实时音频，格式响应校验拒绝小数采样率/声道数。
实际性能与声学结果只归属上述 M5 build.17 用户日志，其他板不能继承。


### build.19 微信单向视频与 UI 状态读取

ESP-IDF v5.5.5；应用 BIN SHA-256：
`6f0a0a7b1713c962247f90a441ee56b1aa04e326dd0c2dddb6cc386619bd7adb`；
ELF SHA-256：`67e4b57e9331d4f3066715797db05f1949724f56f425115d85f26e25072fd1a1`。
匹配 BIN/ELF 已保存于工程 `artifacts/1.0.1+build.19/`。

微信能力上报 MJPEG 上行 / none 下行，通话确认后发送视频流 1。
联系人详情可选择语音或单向视频；语音请求不发送摄像头画面。M5 方向仍为 180°。
普通 UI 状态读取不再扫描任务栈，诊断页与 status 仍可测量。
主机测试覆盖请求、接听、流编号、摄像头退出边界和 UI 周期；原生构建及 BIN/ELF 身份通过。
此版本没有代理烧录或实板日志，微信画面、音视频并行、双讲与卡顿改善仍待验证。
