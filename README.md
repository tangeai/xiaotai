# 小钛多平台设备固件

[快速体验](README.md) · [架构与二次开发](ARCHITECTURE.md) · [参与开发](CONTRIBUTING.md) · [完整文档](docs/README.md)

小钛是面向带屏和无屏音视频设备的开源固件。当前版本支持 H5（网页端）实时查看与双向对讲、AI（人工智能）对话、设备呼叫和微信网络通话。

从下表选择开发板、下载固件，再按对应指南烧录和配网。二次开发见页首的架构与开发文档。

本仓库提供设备固件，不包含完整的云端服务和微信小程序。在线功能还依赖设备绑定与
信令平台、TiRTC 服务，以及所选业务的 AI 或微信服务配置。使用发布包可按下文体验；
接入自建平台时还需配置服务地址和授权凭证，单独编译固件不会自动开通这些服务。

## 选择开发板和固件

先按实物型号、PCB 版本和屏幕变体选板，再下载同一行的固件。板卡 ID 是构建和
固件文件名中的唯一标识；芯片相同的开发板也不能互刷固件。

下表为截至 2026-10-22 的发布版本，各板 build 编号独立计数。
首次使用 ESP32 推荐下载“完整镜像（BIN）”；BK7258 下载“固件包（ZIP）”后解压。

| 开发板图片 | 型号与板卡 ID | 固件版本 | 固件下载 | 文档入口 |
|---|---|---|---|---|
| <img src="https://xiaotai.chat/v1/board-images/b33c12c9b636cf26b96e1fade39d082b56e7b5fbb3d7eb93be1f2b201def205e.png" alt="立创·实战派 BK7258 开发板" width="180"> | 立创·实战派 BK7258<br>`lckfb-bk7258` | v1.0.1<br>build.56 | 完整镜像：包含在 ZIP 中<br>[固件包（ZIP）](https://github.com/tangeai/xiaotai/releases/download/bk7258-v1.0.1/lckfb-bk7258-1.0.1%2Bbuild.56-validation.zip) | [烧录指南](docs/boards/lckfb-bk7258/README.md#烧录)<br>[开发板资料](docs/boards/lckfb-bk7258/README.md#规格)<br>[发布说明](https://github.com/tangeai/xiaotai/releases/tag/bk7258-v1.0.1) |
| <img src="https://xiaotai.chat/v1/board-images/2b74eed62b57852dff00fe9a04b14748bfcca4bbba3b4d79e891f9299f9a0c59.png" alt="立创·实战派 ESP32-S3 开发板" width="180"> | 立创·实战派 ESP32-S3<br>PCB V1.0.1<br>`lckfb-esp32s3` | v1.0.1<br>build.2 | [完整镜像（BIN）](https://github.com/tangeai/xiaotai/releases/download/esp32s3-v1.0.1/lckfb-esp32s3-1.0.1%2Bbuild.2-merged.bin)<br>[固件包（ZIP）](https://github.com/tangeai/xiaotai/releases/download/esp32s3-v1.0.1/lckfb-esp32s3-1.0.1%2Bbuild.2-validation.zip) | [烧录指南](docs/boards/lckfb-esp32s3/README.md#烧录)<br>[开发板资料](docs/boards/lckfb-esp32s3/README.md#规格)<br>[发布说明](https://github.com/tangeai/xiaotai/releases/tag/esp32s3-v1.0.1) |
| <img src="https://xiaotai.chat/v1/board-images/eb94bd88265ac488dc8fbc20fcb3896438439aa513529429bfb8ae2792be9aa4.jpg" alt="微雪 ESP32-P4-WIFI6-Touch-LCD-4.3 开发板" width="180"> | 微雪 ESP32-P4-WIFI6-Touch-LCD-4.3<br>PCB V1.0<br>`waveshare-esp32p4-touch-lcd-43c-v10` | v1.0.1<br>build.2 | [完整镜像（BIN）](https://github.com/tangeai/xiaotai/releases/download/esp32p4-v1.0.1/waveshare-esp32p4-touch-lcd-43c-v10-1.0.1%2Bbuild.2-merged.bin)<br>[固件包（ZIP）](https://github.com/tangeai/xiaotai/releases/download/esp32p4-v1.0.1/waveshare-esp32p4-touch-lcd-43c-v10-1.0.1%2Bbuild.2-validation.zip) | [烧录指南](docs/boards/waveshare-esp32p4-touch-lcd-43c-v10/README.md#烧录)<br>[开发板资料](docs/boards/waveshare-esp32p4-touch-lcd-43c-v10/README.md#规格)<br>[发布说明](https://github.com/tangeai/xiaotai/releases/tag/esp32p4-v1.0.1) |
| <img src="https://xiaotai.chat/v1/board-images/6ea7dc1e034807bc0de00963cfe625d74ed64d241170eeb207762041554bf19e.png" alt="M5Stack CoreS3-Lite 开发板" width="180"> | M5Stack CoreS3-Lite<br>K128-Lite · ILI9342E 屏幕<br>`m5stack-cores3-lite` | v1.0.1<br>build.42 | [完整镜像（BIN）](https://github.com/tangeai/xiaotai/releases/download/m5stack-cores3-lite-v1.0.1/m5stack-cores3-lite-1.0.1%2Bbuild.42-merged.bin)<br>[固件包（ZIP）](https://github.com/tangeai/xiaotai/releases/download/m5stack-cores3-lite-v1.0.1/m5stack-cores3-lite-1.0.1%2Bbuild.42-validation.zip) | [烧录指南](docs/boards/m5stack-cores3-lite/README.md#烧录)<br>[开发板资料](docs/boards/m5stack-cores3-lite/README.md#规格)<br>[发布说明](https://github.com/tangeai/xiaotai/releases/tag/m5stack-cores3-lite-v1.0.1) |

各入口用途如下：

| 入口 | 用途 |
|---|---|
| 完整镜像（BIN） | ESP32 首次烧录使用的单文件，地址为 `0x0`；会覆盖 Wi-Fi、绑定等 NVS 配置。 |
| 固件包（ZIP） | 包含烧录文件、清单和说明。ESP32 按 `MANIFEST.json` 的 `flash_files` 分文件烧录；BK7258 使用包内 `-all-app.bin` 首次烧录，`-app_pack.rbl` 仅用于 OTA 无线升级。 |
| 烧录指南 | 查看连接方式、下载模式、烧录步骤和重启检查；ESP32 同时提供网页与工程烧录的图文步骤。 |
| 开发板资料 | 查看硬件规格、接口、依赖和原理图资料，以及源码开发、编译与排查说明。 |
| 发布说明 | 查看该版本的功能、修复、验证范围和已知问题。 |

需要保留配置的 ESP32 升级，使用分文件 ZIP，不执行全片擦除，并核对新旧分区布局与
配置兼容性。完整镜像与分文件包任选一种烧录，不要同时添加。

其他已适配开发板见[完整开发板目录](docs/boards/README.md)；全部版本见
[GitHub Releases](https://github.com/tangeai/xiaotai/releases)。没有对应发布包时，按开发板指南从源码编译。

## 烧录

先按上表下载目标板卡的固件，核对板型、版本和完整文件名。完整镜像 BIN 直接选择文件；
固件包 ZIP 先解压，再检查 `MANIFEST.json` 中的板型、版本和烧录地址。
确认文件与实物匹配后，按对应开发板的烧录指南操作。

### ESP32 开发板

有两种文件组织方式，**任选一种，不要混用**。乐鑫的 `write-flash` 按地址写入多个 BIN，
`merge-bin` 可将它们合成一个镜像；具体文件数与地址由工程决定，不能固定理解为“三个 BIN”。
参见[乐鑫合并镜像指引](https://docs.espressif.com/projects/esptool/en/latest/esp32s3/esptool/basic-commands.html#merge-binaries-for-flashing-merge-bin)。

| 开发板 | 单文件：网页中选择的完整文件名 | 地址 | 分文件：从同版 ZIP 解压 |
|---|---|---|---|
| 立创 ESP32-S3 | `lckfb-esp32s3-1.0.1+build.2-merged.bin` | `0x0` | **3 个 BIN**：bootloader、分区表、应用 |
| 微雪 ESP32-P4 | `waveshare-esp32p4-touch-lcd-43c-v10-1.0.1+build.2-merged.bin` | `0x0` | **4 个 BIN**：bootloader、分区表、OTA 初始化数据、应用 |
| M5Stack CoreS3-Lite | `m5stack-cores3-lite-1.0.1+build.42-merged.bin` | `0x0` | **3 个 BIN**：bootloader、分区表、应用 |

完整 `-merged.bin` 只添加一行，烧录会覆盖 Wi-Fi、绑定等 NVS 配置。
分文件方式按下方各板指南的“文件名 → 地址”表逐项填写，不能漏文件；
应用 BIN 不能当作合并镜像写到 `0x0`，ZIP、ELF 和 JSON 不能直接烧录。

普通操作不额外要求校验 ZIP。分文件 BIN 的 SHA-256 在包内 `MANIFEST.json` 中；
merged.bin 的 SHA-256 在对应烧录指南及发布页中，按需核验。
写入完成校验与文件来源核对是两件事，正常解压不代表板型、版本和烧录地址正确。

提供两种方式：**网页烧录**无需安装开发环境，适合直接体验；**工程烧录**通过
ESP-IDF 编译并执行 `idf.py flash monitor`，适合修改源码与调试。
网页工具使用 Chrome 或 Edge 打开[乐鑫 ESP Web Tool](https://espressif.github.io/esptool-js/)。

按板型打开图文指南，截图中的红框标出了需要点击或修改的位置：

- [立创 ESP32-S3：网页与工程烧录](docs/boards/lckfb-esp32s3/README.md#烧录)
- [微雪 ESP32-P4：网页与工程烧录](docs/boards/waveshare-esp32p4-touch-lcd-43c-v10/README.md#烧录)
- [M5Stack CoreS3-Lite：网页与工程烧录](docs/boards/m5stack-cores3-lite/README.md#烧录)

分文件 ZIP 需要按 `MANIFEST.json` 的 `flash_files` 完整添加文件和地址，不能只写
应用镜像，也不能把所有文件都填成 `0x0`。下载模式、串口和重启步骤见上述指南。

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
