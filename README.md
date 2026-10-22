# 小钛多平台设备固件

[快速体验](README.md) · [架构与二次开发](ARCHITECTURE.md) · [参与开发](CONTRIBUTING.md) · [完整文档](docs/README.md)

小钛是面向带屏和无屏音视频设备的开源固件。当前版本支持 H5（网页端）实时查看与双向对讲、AI（人工智能）对话、设备呼叫和微信网络通话。

这份 README 只解决三个问题：应该下载哪个固件、怎样烧录、上电后怎样体验。若要阅读实现原理或新增开发板，请使用页首的其他入口。

本仓库提供设备固件，不包含完整的云端服务和微信小程序。在线功能还依赖设备绑定与
信令平台、TiRTC 服务，以及所选业务的 AI 或微信服务配置。使用发布包可按下文体验；
接入自建平台时还需配置服务地址和授权凭证，单独编译固件不会自动开通这些服务。

## 选择开发板和固件

优先从 [GitHub Releases](https://github.com/tangeai/xiaotai/releases) 下载已经打包的验证固件。压缩包名称以板卡 ID（开发板在本仓库中的唯一名称，用于选择编译目标和核对固件包）开头，不要只按芯片名称选择固件。

以下为截至 2026-10-22 的最新发布包，均为 v1.0.1；各开发板的 build 编号独立计数。下载时请核对完整型号：

| 开发板图片 | 对外型号 | 板卡 ID | 下载固件 | 烧录说明 |
|---|---|---|---|---|
| <img src="https://xiaotai.chat/v1/board-images/b33c12c9b636cf26b96e1fade39d082b56e7b5fbb3d7eb93be1f2b201def205e.png" alt="立创·实战派 BK7258 开发板" width="180"> | 立创·实战派 BK7258 | `lckfb-bk7258` | [下载 v1.0.1 · build.56](https://github.com/tangeai/xiaotai/releases/download/bk7258-v1.0.1/lckfb-bk7258-1.0.1%2Bbuild.56-validation.zip)<br>[发布说明](https://github.com/tangeai/xiaotai/releases/tag/bk7258-v1.0.1) · [SHA256SUMS](https://github.com/tangeai/xiaotai/releases/download/bk7258-v1.0.1/SHA256SUMS) | [开发板指南](docs/boards/lckfb-bk7258/README.md) |
| <img src="https://xiaotai.chat/v1/board-images/2b74eed62b57852dff00fe9a04b14748bfcca4bbba3b4d79e891f9299f9a0c59.png" alt="立创·实战派 ESP32-S3 开发板" width="180"> | 立创·实战派 ESP32-S3（PCB V1.0.1） | `lckfb-esp32s3` | [下载 v1.0.1 · build.2](https://github.com/tangeai/xiaotai/releases/download/esp32s3-v1.0.1/lckfb-esp32s3-1.0.1%2Bbuild.2-validation.zip)<br>[发布说明](https://github.com/tangeai/xiaotai/releases/tag/esp32s3-v1.0.1) · [SHA256SUMS](https://github.com/tangeai/xiaotai/releases/download/esp32s3-v1.0.1/SHA256SUMS) | [开发板指南](docs/boards/lckfb-esp32s3/README.md) |
| <img src="https://xiaotai.chat/v1/board-images/eb94bd88265ac488dc8fbc20fcb3896438439aa513529429bfb8ae2792be9aa4.jpg" alt="微雪 ESP32-P4-WIFI6-Touch-LCD-4.3 开发板" width="180"> | 微雪 ESP32-P4-WIFI6-Touch-LCD-4.3（V1.0） | `waveshare-esp32p4-touch-lcd-43c-v10` | [下载 v1.0.1 · build.2](https://github.com/tangeai/xiaotai/releases/download/esp32p4-v1.0.1/waveshare-esp32p4-touch-lcd-43c-v10-1.0.1%2Bbuild.2-validation.zip)<br>[发布说明](https://github.com/tangeai/xiaotai/releases/tag/esp32p4-v1.0.1) · [SHA256SUMS](https://github.com/tangeai/xiaotai/releases/download/esp32p4-v1.0.1/SHA256SUMS) | [开发板指南](docs/boards/waveshare-esp32p4-touch-lcd-43c-v10/README.md) |
| <img src="https://xiaotai.chat/v1/board-images/6ea7dc1e034807bc0de00963cfe625d74ed64d241170eeb207762041554bf19e.png" alt="M5Stack CoreS3-Lite 开发板" width="180"> | M5Stack CoreS3-Lite（K128-Lite，ESP32-S3，ILI9342E） | `m5stack-cores3-lite` | [下载 v1.0.1 · build.42](https://github.com/tangeai/xiaotai/releases/download/m5stack-cores3-lite-v1.0.1/m5stack-cores3-lite-1.0.1%2Bbuild.42-validation.zip)<br>[完整 merged.bin](https://github.com/tangeai/xiaotai/releases/download/m5stack-cores3-lite-v1.0.1/m5stack-cores3-lite-1.0.1%2Bbuild.42-merged.bin)<br>[发布说明](https://github.com/tangeai/xiaotai/releases/tag/m5stack-cores3-lite-v1.0.1) · [SHA256SUMS](https://github.com/tangeai/xiaotai/releases/download/m5stack-cores3-lite-v1.0.1/SHA256SUMS) | [开发板资料与烧录指南](docs/boards/m5stack-cores3-lite/README.md) |

固件包包含对应板卡的烧录文件、烧录配置和校验信息。ESP32 按清单完整烧录多个文件；
BK7258 首次烧录使用包内以 `-all-app.bin` 结尾的文件，以 `-app_pack.rbl` 结尾的文件用于 OTA（无线升级）。

M5 已由用户确认完成实板验证，v1.0.1 · build.42 为正式发布；已知问题见[封版记录](docs/boards/m5stack-cores3-lite/FREEZE.md)。
本发布包仅适用于上表的 ILI9342E 屏幕变体。

其他已经适配的开发板及其型号、固件名称和使用说明统一收录在
[完整开发板目录](docs/boards/README.md)。

发布包内的 `MANIFEST.json` 记录板卡、固件版本、文件校验值和烧录信息；解压后先核对
清单，再连接设备。

没有对应发布包时，可以从源码编译。不要使用其他板卡的固件，也不要把“芯片相同”理解为 GPIO（通用输入输出引脚）、屏幕、音频和烧录布局相同。

## 烧录

先打开 [GitHub Releases](https://github.com/tangeai/xiaotai/releases)，下载名称以目标板卡 ID 开头的最新发布包。不要下载其他开发板的包，即使它们使用相同芯片。解压后检查 `MANIFEST.json`（发布包清单）中的开发板型号、固件版本、SHA-256 校验值和烧录地址，再按下面的方法写入。

### ESP32 开发板

如发布页提供以 `-merged.bin` 结尾的完整镜像，首次烧录可只选这个文件，地址填
`0x0`。它与分文件 ZIP 并列，内容包含启动所需的全部烧录镜像；烧录会覆盖 NVS 中的
Wi-Fi、绑定等持久配置。需要保留配置时，使用下述分文件方式并核对版本兼容性。

ESP32 发布包包含多个烧录文件。使用 Chrome 或 Edge 打开[乐鑫官方 ESP Web Tool](https://espressif.github.io/esptool-js/)，连接开发板串口，然后按照 `MANIFEST.json` 的 `flash_files` 逐项选择文件并填写地址。不能只写入应用镜像，也不要自行猜测地址。Safari 不支持该工具。

如果电脑已经安装 ESP-IDF（乐鑫物联网开发框架），也可以使用发布包记录的参数，通过 `esptool.py` 烧录。各开发板的下载模式、串口连接和操作步骤见上表中的“开发板指南”。

### BK7258 开发板

下载 `lckfb-bk7258-` 开头的发布包，使用包内以 `-all-app.bin` 结尾的文件，从地址 `0x0` 完整烧录。以 `-app_pack.rbl` 结尾的文件是 OTA（无线升级）镜像，不能代替首次完整烧录。烧录工具、下载模式和串口设置见[立创·实战派 BK7258 烧录指南](docs/boards/lckfb-bk7258/README.md#烧录)。

量产或升级前，再次核对板卡 ID、PCB（印刷电路板）版本和 SHA-256。校准区、密钥区和设备身份数据不能被通用固件覆盖。

## 首次体验

确认发布包已经完整烧录且设备正常重启，再进行下面的配网和绑定。不同开发板的屏幕、
按键和功能范围可能不同，以对应的开发板指南为准。

### 1. 配置 Wi-Fi

首次烧录、清除过 Wi-Fi 或已保存的网络连接失败时，设备会显示或播报配网提示，并广播
无密码的 `XiaoTai-XXXX` 热点。用手机或电脑连接该热点；提示“无互联网”属于正常现象。
配网页没有自动打开时，访问 `http://192.168.6.1`，选择 2.4 GHz Wi-Fi，输入路由器密码
并提交。热点重新出现表示联网失败，需要检查密码和信号覆盖。
设备屏和手机配网页的步骤、状态及固定文案以[产品需求 4.11](docs/product/PRODUCT_REQUIREMENTS.md#411-启动无网络与-ap-配网)为准。

### 2. 绑定设备

联网后，设备会显示或播报 6 位验证码。让手机或电脑恢复互联网连接，登录
[小钛设备页](https://xiaotai.chat/devices)，在添加设备处输入验证码。验证码过期时使用
设备新生成的号码。设备进入首页或“准备就绪”状态，并出现在设备列表中，表示绑定成功。

### 3. 验证与恢复

先检查开发板已有的屏幕、触摸、按键、摄像头、麦克风和扬声器，再验证 H5（网页端）
实时画面与双向对讲、至少三轮 AI（人工智能）对话、设备呼叫和微信 VoIP（网络语音通话）。
只测试开发板指南标明支持的功能。每次会话结束后，设备应回到首页或“准备就绪”状态，
且没有异常重启。

触摸屏设备需要更换网络或重新绑定时，在“设备设置”中选择“重置设备”，确认后重新配网
并绑定；无触摸屏设备按对应开发板指南使用串口命令。不要使用全片擦除。遇到音视频问题时，
应记录板卡 ID、固件版本、问题方向和完整日志，
例如明确说明是“网页到设备无声”还是“设备到网页无声”。

## 从源码编译

先下载源码并检查板卡清单：

```bash
git clone https://github.com/tangeai/xiaotai.git
cd xiaotai
python3 tools/build.py --list-boards
python3 tools/build.py --validate
```

工具链不会全部提交到仓库。请从[开发板目录](docs/boards/README.md)打开目标板的完整
使用指南，其中包含环境、依赖、编译和烧录步骤。不要复制其他机器生成的 `build/`、
`sdkconfig`、无线网络配置或设备凭证。

根目录的 `tools/build.py` 是唯一跨平台构建入口：

```bash
# 查看实际工程目录和命令，不执行编译
python3 tools/build.py --board <board-id> --dry-run

# 编译一块开发板
python3 tools/build.py --board <board-id>

# 已安装所有平台工具链时，依次编译全部开发板
python3 tools/build.py --all --keep-going
```

当前 ESP32-S3 / ESP32-P4 工程使用 **ESP-IDF v5.5.5**，构建前执行
`. tools/activate_esp_idf.sh`，并用 `idf.py --version` 核对。

ESP-IDF 和 Beken 仍是两个独立 SDK（软件开发工具包）工程。统一入口只负责选板和分发，
不会把两套构建系统混在一起。开发和交付要求见[参与开发](CONTRIBUTING.md)。

## 下一步

- 想了解功能怎样实现：阅读[架构与二次开发](ARCHITECTURE.md)。
- 想修改代码或新增开发板：阅读[参与开发](CONTRIBUTING.md)。
- 想查询某块板的规格、原理图和依赖：打开[开发板目录](docs/boards/README.md)。
- 想查全部产品与工程文档：打开[完整文档](docs/README.md)。

本项目自有源码和文档采用 [MIT License](LICENSE)。第三方 SDK、预编译库、模型、字体和媒体资源仍遵守各自许可证，详见[第三方清单](THIRD_PARTY.md)。
