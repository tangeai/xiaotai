# 微雪 ESP32-P4-WIFI6-Touch-LCD-4.3

本文中的 H5 指网页端，AI 指人工智能对讲，AEC 指声学回声消除，
VoIP 指网络语音通话。Flash 是闪存，PSRAM 是用于音视频缓冲的大块运行内存。

如果只想体验小钛，直接下载发布固件，按“烧录”和“首次使用”操作即可，不需要先搭建
开发环境。

准备修改或编译源码时，需要掌握 C/C++、Git、串口日志和乐鑫 ESP-IDF 的基本
开发流程。ESP32-P4 的 Wi-Fi 由 ESP32-C6 协同提供，二次开发还需要了解 ESP-Hosted
（P4 与 C6 的联网方案）的主从机关系。建议先阅读
[ESP32-P4 ESP-IDF 5.5.4 入门指南](https://docs.espressif.com/projects/esp-idf/en/v5.5.4/esp32p4/get-started/)
和[微雪官方板卡文档](https://docs.waveshare.net/ESP32-P4-WIFI6-Touch-LCD-4.3/)。

## 烧录

### 直接烧录发布包

从 [GitHub Releases](https://github.com/tangeai/xiaotai/releases) 下载名称以
`waveshare-esp32p4-touch-lcd-43c-v10-` 开头的压缩包。解压后先核对 `MANIFEST.json`
中的板卡 ID、SHA-256 和 `flash_files`（烧录文件及地址列表），再按包内 `README.txt` 的命令烧录。

也可以使用[乐鑫官方 ESP Web Tool](https://espressif.github.io/esptool-js/)。在 Chrome 或
Edge 中连接 P4 的烧录串口，按 `flash_files` 逐项添加文件和地址后执行 `Program`。
该工具不会自动识别板型或地址，必须烧录完整文件集合；Safari 不支持。这里烧录的是
P4 主芯片，不要把同一组文件写入板载 C6。

### 从源码编译后烧录

`idf.py` 会调用 ESP-IDF 自带的 `esptool`：

```bash
cd firmware/esp-idf/esp32p4/waveshare-touch-lcd-43c
idf.py -p <串口> flash monitor
```

烧录前核对完整型号和 PCB V1.0。普通升级不执行 `erase-flash`；C6 slave
固件如需单独升级，必须使用工程锁定版本并按厂商接口操作。

## 首次使用

确认 P4 发布包中的全部烧录文件都已写入且设备正常重启，再进行下面的配网和绑定。

### 1. 配置 Wi-Fi

首次烧录、清除过 Wi-Fi 或已保存网络连接失败时，屏幕显示配网引导，并广播无密码的
`XiaoTai-XXXX` 热点。用手机连接该热点；提示“无互联网”属于正常现象。配网页没有
自动打开时，访问 `http://192.168.6.1`，选择 2.4 GHz Wi-Fi，输入路由器密码并提交。
热点重新出现表示联网失败，需要检查密码、信号覆盖和 ESP-Hosted（P4 与 C6 的联网方案）。
配网页面及文案不在板卡手册中另行定义，统一以[产品需求 4.11](../../product/PRODUCT_REQUIREMENTS.md#411-启动无网络与-ap-配网)为准。

### 2. 绑定设备

联网后，屏幕显示 6 位验证码和二维码。让手机恢复互联网连接，扫描二维码或打开
[小钛设备页](https://xiaotai.chat/devices)，在添加设备处输入验证码。验证码过期时使用
屏幕刷新后的号码。设备进入首页/在线状态后，才表示绑定成功。

### 3. 验证与恢复

依次检查屏幕、触摸、摄像头和音频，再验证 H5（网页端）双向对讲、至少三轮 AI 对话、
设备呼叫和微信 VoIP（网络语音通话）。每次会话结束后应回到首页，且无异常重启、持续
内存下降或异常音视频计数。

配网页打不开时确认手机仍连接设备热点；绑定页没有验证码时检查 IP 地址、系统时间和
服务发现。需要恢复时使用对应的 Wi-Fi 或绑定清除入口，不要擦除整片 Flash。

## 规格

| 项目 | 内容 |
| --- | --- |
| 板卡 ID（仓库中的开发板唯一名称） | `waveshare-esp32p4-touch-lcd-43c-v10` |
| 芯片与平台 | ESP32-P4 / ESP-IDF 5.5.4 |
| 存储 | 32 MB Flash、32 MB PSRAM |
| 显示与触摸 | 4.3 英寸 800×480 横屏产品布局，ST7701 DSI、GT911 |
| 音频 | ES7210 采集、ES8311 播放，支持 AEC |
| 摄像头 | OV5647 MIPI-CSI |
| 网络 | ESP32-C6 + ESP-Hosted/SDIO |
| 产品能力 | H5、AI、设备呼叫、微信 VoIP、屏幕与触摸交互 |

实物以完整型号、商品编号 33875 和 PCB（印刷电路板）V1.0 丝印为准。
本板只能使用与 `waveshare-esp32p4-touch-lcd-43c-v10` 对应的配置和固件包。

## 音视频参数

H5、设备呼叫、多人对讲和微信通话使用 8 kHz 单声道 A-law，每包 20 毫秒、160 字节。
AI 使用 16 kHz 单声道 Opus，每包 20 毫秒，目标码率 16 kbit/s。

H5 视频为 H.264 1280×960、20 fps（每秒 20 帧）、目标码率 3 Mbit/s；设备保持传感器方向，H5 接收端按上报的 270° 顺时针角度旋转。
设备呼叫为 H.264 Annex-B 640×480、5 fps、600 kbit/s、QP 30–46；800×640 传感器完整画面等比缩放后左右留边；微信通话为
960×720、12 fps、1.5 Mbit/s；设备端不旋转，微信接收端按 180° 顺时针角度旋转。

音频先在 16 kHz PCM 域完成 AEC（声学回声消除）。AI 上行继续执行激进 NS（噪声抑制）
和目标 -3 dBFS 的数字 AGC（自动增益控制）后编码 Opus；8 kHz 通话链路降采样后执行
最大 30 dB、目标 -1 dBFS、带限幅的 AGC 并编码 A-law；运行日志周期性报告 AGC
前后平均幅度、峰值和处理失败数，失败帧回退原始 PCM，不会编码未定义数据。设置页可用相同的减/加控件
调节音量和 1–5 档麦克风灵敏度，灵敏度默认 4 档。

Opus 编码和解码工作任务各使用 40 KiB PSRAM 栈，并周期输出栈最低余量；该预算覆盖
SILK/CELT 的深调用路径，不能退回通用音频任务的 6 KiB 栈配置。
完整流编号见[音视频参数与媒体处理](../../product/MEDIA_CONTRACT.md)。

设备显示六位绑定码时，请使用浏览器打开 `https://xiaotai.chat`，进入设备绑定并输入验证码。

微信通话的音频收发已经统一使用协议规定的流 0，主机测试会检查实现常量。

TODO：下一次实板回归时，重新保存主叫、被叫、上行首包和双向通话证据。
这是证据补充项，不阻塞当前源码提交。

## 环境与依赖

| 依赖项 | 固定版本或文件 | 来源或下载链接 | SHA-256 |
| --- | --- | --- | --- |
| 主机环境 | Linux/WSL2、Git、Python 3 | [Git](https://git-scm.com/downloads)、[Python](https://www.python.org/downloads/) | 无 |
| ESP-IDF | `v5.5.4`，提交 `735507283d5b2f9fb363a1901172dbd9e847945d` | [Espressif 官方仓库](https://github.com/espressif/esp-idf/tree/735507283d5b2f9fb363a1901172dbd9e847945d) | 无 |
| C/C++ 编译器 | `riscv32-esp-elf-gcc 14.2.0`，构建标识 `esp-14.2.0_20260121` | [ESP-IDF 工具清单](https://github.com/espressif/esp-idf/blob/735507283d5b2f9fb363a1901172dbd9e847945d/tools/tools.json) | 无，安装器按工具清单校验下载文件 |
| TiRTC | 2.3.0，`libTiRTC.a` | [ESP32-P4 发布目录](https://repo-sdk.tange-ai.com/service/rest/repository/browse/tirtc-sdks/releases/espressif_esp32p4/) | `a7a01ffd496a55364c7e4d665ff3884d078147bba96752a965d97befca12e451` |
| ESP-IDF 组件 | ESP-Hosted 1.4.7、ESP-SR 2.4.6、`esp_h264` 1.3.8、`esp_codec_dev` 1.5.10、Waveshare 4.3 BSP v1.0.1 | [工程依赖锁定文件](../../../firmware/esp-idf/esp32p4/waveshare-touch-lcd-43c/dependencies.lock) | 无 |
| C6 从机固件 | 与当前主工程匹配的版本 | 无 | 无 |

组件的完整解析版本以工程依赖锁定文件和 TiRTC
[校验清单](../../../firmware/esp-idf/esp32p4/waveshare-touch-lcd-43c/components/tirtc_sdk/SHA256SUMS.txt)为准。

## 原理图与硬件资料

| 资料名称 | 版本或标识 | 链接 | SHA-256 | 资料状态 |
| --- | --- | --- | --- | --- |
| 微雪官方产品文档 | SKU 33874/33875 | [在线文档](https://docs.waveshare.net/ESP32-P4-WIFI6-Touch-LCD-4.3/) | 无 | 已提供 |
| 官方相关资料页 | 无 | [在线文档](https://docs.waveshare.net/ESP32-P4-WIFI6-Touch-LCD-4.3/Resources-And-Documents/) | 无 | 已提供 |
| ESP32-P4-WIFI6-Touch-LCD-4.3 原理图 | 实物 PCB V1.0 | [PDF](https://www.waveshare.net/w/upload/b/b8/ESP32-P4-WIFI6-Touch-LCD-4.3-schematic.pdf) | `3697baa3ded0089446baf09705f437d13cf0324874031ccc57fd9b72cd9dfe53` | 已提供 |
| 微雪官方参考仓库 | 核对提交 `74d1a81` | [GitHub](https://github.com/waveshareteam/ESP32-P4-WIFI6-Touch-LCD-4.3)、[Gitee](https://gitee.com/waveshare/esp32-p4-wifi6-touch-lcd-4.3) | 无 | 已提供 |

原理图对应实物版本、BSP 版本和实测差异记录在工程 Hardware IR（硬件信息记录）中。

## 开发

### 目录结构

从仓库根目录看，这块板相关的代码分布如下：

```text
xiaotai/
├── boards/waveshare/esp32p4-touch-lcd-43c/ # 板卡身份、硬件参数与媒体接入
├── product/                                # 跨芯片复用的业务状态、协议与测试
├── platforms/esp-idf/waveshare_p4/         # P4 摄像头、显示、内存和板级公共实现
└── firmware/esp-idf/esp32p4/waveshare-touch-lcd-43c/
    ├── main/app_main.c                     # 固件启动入口
    ├── main/application/                   # 产品状态、配置、呼叫和微信业务装配
    ├── main/connectivity/                  # P4 与 C6 的联网流程
    ├── main/drivers/                       # 音频、显示和虚拟媒体驱动
    ├── main/media/                         # 摄像头管线和媒体负载控制
    ├── main/protocols/                     # HTTP 与 TiRTC 协议接入
    ├── main/services/                      # AI、绑定、设备呼叫、微信、OTA 和媒体服务
    ├── main/ui/                            # 页面、布局、字体和界面资源
    ├── main/platform/                      # 存储、时间、日志和任务策略
    ├── components/                         # BSP、ESP-Hosted 和公共组件接入
    └── *-contract.json                     # 硬件、交互和媒体契约
```

`build/` 是构建生成目录，不是源码入口。依赖组件通过 `idf_component.yml` 和锁定文件
更新，不要直接修改 `managed_components/` 中的下载副本。

### 按任务查找代码

| 要修改的内容 | 首先查看 | 边界说明 |
| --- | --- | --- |
| 板卡型号、引脚和媒体入口 | `boards/waveshare/esp32p4-touch-lcd-43c/` | 只放该 PCB 和外设组合特有的事实 |
| 启动流程和产品装配 | 工程 `main/app_main.c`、`main/application/` | 业务状态与底层生命周期分开 |
| H5、AI、设备呼叫和微信 VoIP 的状态与优先级 | `product/src/`、`main/application/` | 公共规则优先落在可测试的产品接口 |
| C6 联网、配网和网络状态 | `main/connectivity/`、工程 `components/espressif__esp_hosted/` | P4 不直接假设 Wi-Fi 已就绪 |
| 麦克风、扬声器、AEC 和播放控制 | `main/drivers/audio/`、`main/services/audio_playout_controller.c` | 同时核对音频契约和实际播放参考 |
| MIPI-CSI 摄像头、视频转换和负载控制 | `main/media/`、`platforms/esp-idf/waveshare_p4/` | 采集、转换和业务编码参数分层维护 |
| TiRTC 连接、流编号和媒体桥接 | `main/protocols/tirtc/`、`main/services/rtc_media_bridge.c` | 不把 H5、设备呼叫和微信流配置混用 |
| AI、设备绑定、设备呼叫、微信和 OTA | `main/services/` 下对应目录或文件 | 服务处理协议，业务所有权仍由应用层决定 |
| 页面、触摸反馈和显示布局 | `main/ui/`、`platforms/esp-idf/waveshare_p4/` | 页面不直接接管会话状态 |
| 回归测试和构建约束 | `product/tests/`、`tools/tests/` | 修改行为时先补失败用例，再改实现 |

### 推荐修改顺序

1. 先读根目录 [`AGENTS.md`](../../../AGENTS.md)、[`ARCHITECTURE.md`](../../../ARCHITECTURE.md)
   和[测试说明](../../../tests/README.md)，确认改动属于产品、服务、平台还是板级代码。
2. 修改媒体链路前核对 `board-audio-contract.json`、`board-video-contract.json` 和
   `platform-media-contract.json`，明确业务类型、流编号、格式和资源预算。
3. 先修改公共业务接口和主机测试，再接入 P4/C6、摄像头、显示或音频实现。
4. 运行本页“测试”中的命令，最后上板验证双向媒体、会话互斥和资源释放。

## 编译

```bash
bash tools/setup_esp_idf.sh esp32p4
. tools/activate_esp_idf.sh
python3 tools/build.py --board waveshare-esp32p4-touch-lcd-43c-v10
```

工程目录为 `firmware/esp-idf/esp32p4/waveshare-touch-lcd-43c/`。

## 测试

```bash
bash tools/check.sh
python3 tools/build.py --board waveshare-esp32p4-touch-lcd-43c-v10
```

实板验证要覆盖显示与触摸方向、C6 联网、摄像头关键帧、H5、AI、设备呼叫和微信
VoIP。音视频分别记录实际帧率、码率、丢帧、首包、AEC 参考不足和会话释放结果。

## 排查

- 无法联网：先检查 C6 slave 固件版本、SDIO 初始化和 ESP-Hosted 状态。
- 视频方向或比例错误：分别核对 H5 1280×960、设备呼叫 640×480 和微信 960×720 编码配置。设备互呼保持当前方向协商；微信双向分别核对“设备到小程序 `camera_rotation=180`”和“小程序到设备 `down_video_rotation=1` 加 P4 本地顺时针 90 度”，不要共用一个旋转值。
- 单向无声：核对业务类型、音频流编号、订阅状态和首包统计，微信音频固定使用流 0。
- 会话结束后异常：检查旧 generation（会话代次）回调是否被拒绝，以及相机和音频资源是否释放。

## 验证状态与已知限制

项目方确认当前固件已在该型号的 PCB V1.0 实板跑通。当前状态标记为“已验证”。重新
测试后，验证人员需要同步记录测试状态和对应固件身份。

板级配置位于 `boards/waveshare/esp32p4-touch-lcd-43c/`；硬件事实和证据等级见工程
`hardware-ir.json`。
