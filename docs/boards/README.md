# 已支持开发板

本页按 `boards/*/*/board.json` 维护。板卡编号是构建、固件包和日志中的永久身份。
选择板卡时还要核对完整型号和印刷电路板版本；同一芯片的固件也不一定能互换。

表中的 AEC 指声学回声消除。Flash 指闪存，PSRAM 指用于音视频缓冲的大块运行内存。

| 板卡 ID（仓库中的开发板唯一名称） | 厂商与型号 | 平台 | 资源 | 能力 | 当前状态 | 说明 |
|---|---|---|---|---|---|---|
| `alientek-atk-dnesp32s3` | ALIENTEK ATK-DNESP32S3 V1.4 | ESP32-S3 / ESP-IDF | 16 MB Flash / 8 MB PSRAM | 音频、按键、MJPEG 摄像头、Wi-Fi | 实板已跑通 | [打开](alientek-atk-dnesp32s3/README.md) |
| `lckfb-esp32s3` | 立创·实战派 ESP32-S3（ESP32-S3-WROOM-1-N16R8，PCB V1.0.1） | ESP32-S3 / ESP-IDF | 16 MB Flash / 8 MB PSRAM | AEC、音频、屏幕、触摸、摄像头、Wi-Fi | 实板已跑通 | [打开](lckfb-esp32s3/README.md) |
| `waveshare-esp32p4-touch-lcd-43c-v10` | Waveshare ESP32-P4-WIFI6-Touch-LCD-4.3 | ESP32-P4 / ESP-IDF | 32 MB Flash / 32 MB PSRAM | AEC、音频、屏幕、触摸、摄像头、Hosted Wi-Fi | 实板已跑通 | [打开](waveshare-esp32p4-touch-lcd-43c-v10/README.md) |
| `lckfb-bk7258` | 立创·实战派 BK7258 | BK7258 / Beken AVDK | 8 MiB Flash / 16 MiB PSRAM / 640 KiB SRAM（构建配置） | 音频、屏幕、触摸、H.264 摄像头、Wi-Fi | 实板已跑通 | [打开](lckfb-bk7258/README.md) |

新增板卡时必须同时更新 manifest、板级短 README 和本目录下的完整使用文档；后续会由工具自动生成本表，并在
CI 中检查根 README 的主力板链接。板卡照片缺失是文档缺口，不允许用另一 revision
或另一尺寸板的照片代替。

TODO：逐板补齐与固件 SHA-256 校验值绑定的验证日期、串口日志摘要、功能矩阵、
印刷电路板版本、芯片版本和 Hardware IR（硬件信息记录）。
该事项只补充证据，不改变现有板卡已跑通的结论。

所有板卡使用页遵守[参与开发中的文档要求](../../CONTRIBUTING.md#文档要求)。每块板的
页面都包含硬件、工具链、开发、编译、烧录、测试和排查说明。
