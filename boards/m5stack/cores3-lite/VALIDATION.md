# CoreS3-Lite 验证记录

工具链：ESP-IDF **v5.5.5**，提交 `b774170ff46c393eeb5e495ea37936038d3f4f4f`。
板卡 K128-Lite 的物理 PCB 和屏幕版本未确认。用户 build.9 日志确认 PSRAM 与应用启动，
照片显示花屏；build.10 已由用户运行并报告触摸暂时失去响应、自回声问题。
当前 build.12 包含绑定入口修复与摄像头在顶部的 180° 显示/触摸方向，尚未实板验收。本次未由代理烧录、未发布。
交付模式为本地开发构建，不创建 GitHub Release 或发布包。

| 检查 | 结果 |
|---|---|
| Skill 0.10.1 Doctor：目标编译器、ESP-IDF、TiRTC ABI | PASS；未使用旧 managed kit |
| Hardware IR v2 校验、严格 intake | PASS / READY_TO_PORT；不是实板证明 |
| 新增公共边界回归 | PASS，6 项板级回归 + 5 项 PSRAM 配置回归 + 3 项显示/触摸几何回归；先失败再修正 |
| `python3 tools/build.py --validate` | PASS，5 块板 |
| `python3 -m unittest discover -s tools/tests -v` | PASS，374 项 |
| `bash tools/check.sh` | PASS；产品核心行覆盖率 93.09%、分支覆盖率 63.74% |
| `ili9342c` | build.6 仅历史编译通过，含错误 Octal 配置，禁止继续使用；本次未重建 C 变体 |
| `ili9342e` 原生构建 | PASS，`1.0.1+build.12`，Quad PSRAM / 180° 显示与触摸 / 绑定地址统一，退出码 0 |
| PSRAM 有效配置与最终链接 | PASS，`CONFIG_SPIRAM_MODE_QUAD=y`；ELF map 链接 `esp_psram_impl_ap_quad.c.obj` |
| BIN/ELF 身份、镜像内板名、单一 I²C 驱动族 | PASS |
| 编译输入隔离 | PASS，只编译 M5 板级源码；C 屏定义仅作用于显示组件 |
| 旧固件启动 | FAIL，用户日志：`octal_psram` 初始化失败，在 `mspi_init` abort 并循环复位 |
| build.9 启动 / 内存测试 | PASS（用户日志）；8 MB / 80 MHz，内存测试通过并进入配网；不代表已绑定上线 |
| build.9 显示 | FAIL（用户照片），共享 LVGL 配置覆盖板级方向 |
| build.10 绑定与 AI | 用户日志确认凭证保存、MQTT 在线、AI 准入和上下行计数；声学未通过 |
| build.10 交互与 AEC | FAIL（用户反馈）：触摸暂时无响应，随后恢复；偶发把自身回复识别为用户说话 |
| build.12 显示、触摸、媒体、H5、AI、设备/微信通话、PTT、双讲、稳定性 | SKIP，待实板 |

当前产物属于 **COMPILE_PASS**。该仓库使用共享源码和当前 Opus AI 合同，通过仓库的
行为回归及原生构建门禁验证；未套用独立生成工程的旧 A-law AI 静态合同，也未声称通过
该 Skill 的独立 BUILD_VERIFIED 认证。设备业务与声学可用性仍须实板验收。

## 当前保留产物：ILI9342E

- 应用：`firmware/esp-idf/esp32s3/m5stack-cores3-lite/build/m5stack_cores3_lite_xiaotai.bin`
- 版本：`1.0.1+build.12`；大小：4097808 字节。
- 应用 BIN SHA-256：`c70885d9ef28170ac4ad0456d45720317329ab351a3d72d2f77ffcd3d8d6a7f8`。
- ELF：同目录 `m5stack_cores3_lite_xiaotai.elf`；大小：29137872 字节。
- 完整 ELF SHA-256：`e278d1e71432fd70d12c48bc53df3dbe2d3429496333e8005e8a4a9e8b29512b`；与 BIN 内 `app_elf_sha256` 一致。
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
