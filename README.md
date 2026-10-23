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

下表为截至 2026-10-23 的发布版本，各板 build 编号独立计数。
首次使用 ESP32 推荐下载“完整镜像（BIN）”；BK7258 下载“固件包（ZIP）”后解压。

| 开发板图片 | 型号与板卡 ID | 固件版本 | 固件下载 | 文档入口 |
|---|---|---|---|---|
| <img src="docs/images/boards/lckfb-bk7258.webp" alt="立创·实战派 BK7258 开发板" width="180"> | 立创·实战派 BK7258<br>`lckfb-bk7258` | v1.0.2<br>build.71 | 完整镜像：包含在 ZIP 中<br>[固件包（ZIP）](https://github.com/tangeai/xiaotai/releases/download/bk7258-v1.0.2/lckfb-bk7258-1.0.2%2Bbuild.71-validation.zip) | [烧录指南](docs/boards/lckfb-bk7258/README.md#烧录)<br>[开发板资料](docs/boards/lckfb-bk7258/README.md#规格)<br>[发布说明](https://github.com/tangeai/xiaotai/releases/tag/bk7258-v1.0.2) |
| <img src="docs/images/boards/lckfb-esp32s3.webp" alt="立创·实战派 ESP32-S3 开发板" width="180"> | 立创·实战派 ESP32-S3<br>PCB V1.0.1<br>`lckfb-esp32s3` | v1.0.1<br>build.2 | [完整镜像（BIN）](https://github.com/tangeai/xiaotai/releases/download/esp32s3-v1.0.1/lckfb-esp32s3-1.0.1%2Bbuild.2-merged.bin)<br>[固件包（ZIP）](https://github.com/tangeai/xiaotai/releases/download/esp32s3-v1.0.1/lckfb-esp32s3-1.0.1%2Bbuild.2-validation.zip) | [烧录指南](docs/boards/lckfb-esp32s3/README.md#烧录)<br>[开发板资料](docs/boards/lckfb-esp32s3/README.md#规格)<br>[发布说明](https://github.com/tangeai/xiaotai/releases/tag/esp32s3-v1.0.1) |
| <img src="docs/images/boards/waveshare-esp32p4-touch-lcd-43c-v10.webp" alt="微雪 ESP32-P4-WIFI6-Touch-LCD-4.3 开发板" width="180"> | 微雪 ESP32-P4-WIFI6-Touch-LCD-4.3<br>PCB V1.0<br>`waveshare-esp32p4-touch-lcd-43c-v10` | v1.0.1<br>build.14 | [完整镜像（BIN）](https://github.com/tangeai/xiaotai/releases/download/esp32p4-v1.0.1/waveshare-esp32p4-touch-lcd-43c-v10-1.0.1%2Bbuild.14-merged.bin)<br>[固件包（ZIP）](https://github.com/tangeai/xiaotai/releases/download/esp32p4-v1.0.1/waveshare-esp32p4-touch-lcd-43c-v10-1.0.1%2Bbuild.14-validation.zip) | [烧录指南](docs/boards/waveshare-esp32p4-touch-lcd-43c-v10/README.md#烧录)<br>[开发板资料](docs/boards/waveshare-esp32p4-touch-lcd-43c-v10/README.md#规格)<br>[发布说明](https://github.com/tangeai/xiaotai/releases/tag/esp32p4-v1.0.1) |
| <img src="docs/images/boards/m5stack-cores3-lite.webp" alt="M5Stack CoreS3-Lite 开发板" width="180"> | M5Stack CoreS3-Lite<br>K128-Lite · ILI9342E 屏幕<br>`m5stack-cores3-lite` | v1.0.1<br>build.42 | [完整镜像（BIN）](https://github.com/tangeai/xiaotai/releases/download/m5stack-cores3-lite-v1.0.1/m5stack-cores3-lite-1.0.1%2Bbuild.42-merged.bin)<br>[固件包（ZIP）](https://github.com/tangeai/xiaotai/releases/download/m5stack-cores3-lite-v1.0.1/m5stack-cores3-lite-1.0.1%2Bbuild.42-validation.zip) | [烧录指南](docs/boards/m5stack-cores3-lite/README.md#烧录)<br>[开发板资料](docs/boards/m5stack-cores3-lite/README.md#规格)<br>[发布说明](https://github.com/tangeai/xiaotai/releases/tag/m5stack-cores3-lite-v1.0.1) |
| <img src="docs/images/boards/mgkfb-bk7258.webp" alt="觅感 AI 玩具开发板 BK7258" width="180"> | 觅感 AI 玩具开发板 BK7258<br>双屏眼睛 · 8 MiB Flash<br>`mgkfb-bk7258` | v1.0.3<br>build.20 | 完整镜像：包含在 ZIP 中<br>[固件包（ZIP）](https://github.com/tangeai/xiaotai/releases/download/mgkfb-bk7258-v1.0.3/mgkfb-bk7258-1.0.3%2Bbuild.20-validation.zip) | [烧录指南](docs/boards/mgkfb-bk7258/README.md#烧录)<br>[开发板资料](docs/boards/mgkfb-bk7258/README.md#规格)<br>[发布说明](https://github.com/tangeai/xiaotai/releases/tag/mgkfb-bk7258-v1.0.3) |

选板时请留意：M5 发布镜像只适用于已确认的 ILI9342E 屏；觅感 BK7258 的长期稳定性与
异常恢复尚待专项验证；P4 build.14 的逐项音视频验收记录仍待补齐。详情见各板指南。
P4 build.14 的[对应源码提交](https://github.com/tangeai/xiaotai/tree/c2d75e9eadcc1c24af1bb09b3f9972ebdad84e2b)
与旧发布标签不同，发布页自动生成的 Source code 压缩包不对应 build.14。

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

烧录时可以使用多个 BIN 文件，也可以使用合并后的一个 BIN 文件，**任选一种即可**：

- **烧录多个 BIN 文件**：解压固件包，将多个 BIN 按清单写入各自的地址。
- **烧录合并后的 BIN 文件**：下载完整的 `-merged.bin`，只添加这一个文件，从 `0x0` 写入。

完整镜像由同一版本的分文件 BIN 合并生成，制作方法见[乐鑫官方指引](https://docs.espressif.com/projects/esptool/en/latest/esp32s3/esptool/basic-commands.html#merge-binaries-for-flashing-merge-bin)。

| 开发板 | 合并镜像：只选择这一个 BIN | 烧录地址 | 分文件：从同版 ZIP 解压 |
|---|---|---|---|
| 立创 ESP32-S3 | `lckfb-esp32s3-1.0.1+build.2-merged.bin` | `0x0` | **3 个 BIN**：bootloader、分区表、应用 |
| 微雪 ESP32-P4 | `waveshare-esp32p4-touch-lcd-43c-v10-1.0.1+build.14-merged.bin` | `0x0` | **4 个 BIN**：bootloader、分区表、OTA 初始化数据、应用 |
| M5Stack CoreS3-Lite | `m5stack-cores3-lite-1.0.1+build.42-merged.bin` | `0x0` | **3 个 BIN**：bootloader、分区表、应用 |

完整 `-merged.bin` 只添加一行，烧录会覆盖保存的 Wi-Fi 和绑定信息。
分文件方式按下方各板指南的“文件名 → 地址”表逐项填写，不能漏文件；
应用 BIN 不能当作合并镜像写到 `0x0`，ZIP、ELF 和 JSON 不能直接烧录。

下载后，先确认固件对应的开发板型号和版本，再按指南填写烧录地址。
怀疑文件下载损坏时，可以用发布页或包内清单中的 SHA-256 校验值检查文件。

不修改源码时，可以直接用**网页烧录**，无需安装开发环境。
需要修改和调试固件时，使用**工程烧录**，在 ESP-IDF 中编译并执行 `idf.py flash monitor`。
网页工具使用 Chrome 或 Edge 打开[乐鑫 ESP Web Tool](https://espressif.github.io/esptool-js/)。

按板型打开图文指南，截图中的红框标出了需要点击或修改的位置：

- [立创 ESP32-S3：网页与工程烧录](docs/boards/lckfb-esp32s3/README.md#烧录)
- [微雪 ESP32-P4：网页与工程烧录](docs/boards/waveshare-esp32p4-touch-lcd-43c-v10/README.md#烧录)
- [M5Stack CoreS3-Lite：网页与工程烧录](docs/boards/m5stack-cores3-lite/README.md#烧录)

### BK7258 开发板

两块板使用各自的固件，通过 Beken 下载工具串口烧录，不使用 ESP32 网页烧录工具。
先按下面的准确型号选择；芯片相同不代表固件可以互刷。

#### 立创·实战派 BK7258

板卡 ID：`lckfb-bk7258`，发布版本：v1.0.2 · build.71。

1. 下载并解压 [`lckfb-bk7258-1.0.2+build.71-validation.zip`](https://github.com/tangeai/xiaotai/releases/download/bk7258-v1.0.2/lckfb-bk7258-1.0.2%2Bbuild.71-validation.zip)。
2. 在 Beken 下载工具中选择 **`lckfb-bk7258-1.0.2+build.71-all-app.bin`**，起始地址 **`0x00000000`**，用于首次完整烧录。

`lckfb-bk7258-1.0.2+build.71-app_pack.rbl` 仅用于 OTA（无线升级），不能用于首次完整烧录。

连接方式、下载模式、串口设置与重启检查见[立创·实战派 BK7258 烧录指南](docs/boards/lckfb-bk7258/README.md#烧录)。

#### 觅感 AI 玩具开发板 BK7258

板卡 ID：`mgkfb-bk7258`，发布版本：v1.0.3 · build.20，适用于 **8 MiB Flash、双屏眼睛**的觅感板。

1. 下载并解压 [`mgkfb-bk7258-1.0.3+build.20-validation.zip`](https://github.com/tangeai/xiaotai/releases/download/mgkfb-bk7258-v1.0.3/mgkfb-bk7258-1.0.3%2Bbuild.20-validation.zip)。
2. 用本板通讯 Type-C 连接电脑，在 Beken 下载工具中选择 **`mgkfb-bk7258-1.0.3+build.20-all-app.bin`**，起始地址 **`0x00000000`**，用于首次完整烧录。

`mgkfb-bk7258-1.0.3+build.20-app_pack.rbl` 仅用于 OTA，不能用于首次串口烧录；本板 OTA 尚未专项验收。

连接、复位和烧录后的检查步骤见[觅感 BK7258 烧录指南](docs/boards/mgkfb-bk7258/README.md#烧录)。

请保留设备原有的校准数据和身份信息，按对应指南操作，不要随意全片擦除。

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
并绑定；无触摸屏设备按对应开发板指南使用实体按键或串口命令。不要使用全片擦除。遇到音视频问题时，
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
- 想通过串口查看 BK7258 状态或调试：阅读[BK7258 串口命令](docs/development/BK7258_SERIAL_COMMANDS.md)。
- 想查全部产品与工程文档：打开[完整文档](docs/README.md)。

本项目自有源码和文档采用 [MIT License](LICENSE)。第三方 SDK、预编译库、模型、字体和媒体资源仍遵守各自许可证，详见[第三方清单](THIRD_PARTY.md)。
