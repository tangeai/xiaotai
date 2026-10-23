# 已支持开发板

各板当前问题、功能需求和待验证项见[开发板问题与需求清单](ISSUES_AND_REQUIREMENTS.md)。

本页按 `boards/*/*/board.json` 维护。板卡编号是构建、固件包和日志中的永久身份。
选择板卡时还要核对完整型号和印刷电路板版本；同一芯片的固件也不一定能互换。

表中的 AEC 指声学回声消除。Flash 指闪存，PSRAM 指用于音视频缓冲的大块运行内存。

开发板指南先列当前可下载的固件及适用硬件，再按“烧录工具 → 烧录步骤 → 首次使用”
说明用户操作；分文件烧录、源码编译和硬件参数随后说明。没有正式发布包的板卡明确标出，
只给出可执行的源码构建步骤。各板的 USB 接口、下载模式、烧录地址和镜像数量以本板指南为准。

| 板卡 ID（仓库中的开发板唯一名称） | 厂商与型号 | 平台 | 资源 | 能力 | 当前状态 | 说明 |
|---|---|---|---|---|---|---|
| `alientek-atk-dnesp32s3` | ALIENTEK ATK-DNESP32S3 V1.4 | ESP32-S3 / ESP-IDF 5.5.5 | 16 MB Flash / 8 MB PSRAM | 音频、按键、MJPEG 摄像头、Wi-Fi | 实板已跑通；尚无正式发布包 | [源码烧录指南](alientek-atk-dnesp32s3/README.md) |
| `lckfb-esp32s3` | 立创·实战派 ESP32-S3（ESP32-S3-WROOM-1-N16R8，PCB V1.0.1） | ESP32-S3 / ESP-IDF 5.5.5 | 16 MB Flash / 8 MB PSRAM | AEC、音频、屏幕、触摸、摄像头、Wi-Fi | 实板已跑通 | [下载 v1.0.1 · build.2](https://github.com/tangeai/xiaotai/releases/tag/esp32s3-v1.0.1) · [开发板指南](lckfb-esp32s3/README.md) |
| `waveshare-esp32p4-touch-lcd-43c-v10` | Waveshare ESP32-P4-WIFI6-Touch-LCD-4.3 | ESP32-P4 / ESP-IDF 5.5.5 | 32 MB Flash / 32 MB PSRAM | AEC、音频、屏幕、触摸、摄像头、Hosted Wi-Fi | build.14 已经实板验证可用 | [下载 v1.0.1 · build.14](https://github.com/tangeai/xiaotai/releases/tag/esp32p4-v1.0.1) · [开发板指南](waveshare-esp32p4-touch-lcd-43c-v10/README.md) |
| `lckfb-bk7258` | 立创·实战派 BK7258 | BK7258 / Beken AVDK | 8 MiB Flash / 16 MiB PSRAM / 640 KiB SRAM（构建配置） | 音频、屏幕、触摸、H.264 摄像头、Wi-Fi | 实板已跑通 | [下载 v1.0.2 · build.71](https://github.com/tangeai/xiaotai/releases/tag/bk7258-v1.0.2) · [开发板指南](lckfb-bk7258/README.md) |
| `m5stack-cores3-lite` | M5Stack CoreS3-Lite (K128-Lite) | ESP32-S3 / ESP-IDF 5.5.5 | 16 MB Flash / 8 MB PSRAM | AEC、音频、触摸屏、GC0308、Wi-Fi | 已实板验证；v1.0.1 正式发布 | [下载 v1.0.1 · build.42](https://github.com/tangeai/xiaotai/releases/tag/m5stack-cores3-lite-v1.0.1) · [开发板指南](m5stack-cores3-lite/README.md) |
| `mgkfb-bk7258` | 觅感 AI 玩具开发板 BK7258（PCB 版本待核对） | BK7258 / AVDK SMP | 8 MiB Flash（用户确认）/ PSRAM 待测 | 双屏眼睛/无屏变体、按键、音频、H.264 摄像头、Wi-Fi | 用户已确认开发板验证；release 产物的专项范围见记录 | [下载 v1.0.3 · build.20](https://github.com/tangeai/xiaotai/releases/tag/mgkfb-bk7258-v1.0.3) · [开发板指南](mgkfb-bk7258/README.md) |

固件下载、功能范围和已知问题见各板发布说明；烧录、配网和开发步骤见对应开发板指南。
具体构建的测试范围与历史日志保存在各板验证记录中。

新增板卡时必须同时更新 manifest、板级短 README 和本目录下的完整使用文档；后续会由工具自动生成本表，并在
CI 中检查根 README 的主力板链接。板卡照片缺失是文档缺口，不允许用另一 revision
或另一尺寸板的照片代替。

TODO：逐板补齐与固件 SHA-256 校验值绑定的验证日期、串口日志摘要、功能矩阵、
印刷电路板版本、芯片版本和 Hardware IR（硬件信息记录）。
该事项只补充证据，不改变现有板卡已跑通的结论。

所有板卡使用页遵守[参与开发中的文档要求](../../CONTRIBUTING.md#文档要求)。每块板的
页面都包含硬件、工具链、开发、编译、烧录、测试和排查说明。
