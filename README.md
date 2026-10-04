# 小钛多平台设备固件

[快速体验](README.md) · [架构与二次开发](ARCHITECTURE.md) · [参与开发](CONTRIBUTING.md) · [完整文档](docs/README.md)

小钛是面向带屏和无屏音视频设备的开源固件。当前版本支持 H5（网页端）实时查看与双向对讲、AI（人工智能）对话、设备呼叫和微信网络通话。

这份 README 只解决三个问题：应该下载哪个固件、怎样烧录、上电后怎样体验。若要阅读实现原理或新增开发板，请使用页首的其他入口。

## 选择开发板和固件

优先从 [GitHub Releases](https://github.com/tangeai/xiaotai/releases) 下载已经打包的验证固件。压缩包名称以板卡 ID（开发板在本仓库中的唯一名称，用于选择编译目标和核对固件包）开头，不要只按芯片名称选择固件。

首页优先提供立创·实战派 BK7258 的固件下载入口：

| 对外型号 | 板卡 ID | 下载固件 | 烧录说明 |
|---|---|---|---|
| 立创·实战派 BK7258 | `lckfb-bk7258` | [下载 v1.0.0 固件包](https://github.com/tangeai/xiaotai/releases/download/bk7258-v1.0.0/lckfb-bk7258-1.0.0%2Bbuild.3-validation.zip) · [仅下载完整烧录镜像](https://github.com/tangeai/xiaotai/releases/download/bk7258-v1.0.0/xiaotai-bk7258-v1.0.0-all-app.bin) | [开发板指南](docs/boards/lckfb-bk7258/README.md) |

推荐下载固件包，其中包含完整烧录镜像、OTA（无线升级）镜像、分区表、构建摘要和
SHA-256 校验清单。只需要首次完整烧录时，也可以直接下载 `all-app.bin`。

<details>
<summary>更多开发板</summary>

| 对外型号 | 板卡 ID | 发布包名称前缀 | 烧录说明 |
|---|---|---|---|
| 立创·实战派 ESP32-S3 | `lckfb-esp32s3` | `lckfb-esp32s3-` | [开发板指南](docs/boards/lckfb-esp32s3/README.md) |

其他已经适配的开发板及其型号、固件名称和使用说明统一收录在
[完整开发板目录](docs/boards/README.md)。

</details>

发布包内的 `MANIFEST.json` 记录板卡、源码版本、文件校验值和烧录信息；解压后先核对
清单，再连接设备。

没有对应发布包时，可以从源码编译。不要使用其他板卡的固件，也不要把“芯片相同”理解为 GPIO（通用输入输出引脚）、屏幕、音频和烧录布局相同。

## 烧录

先打开 [GitHub Releases](https://github.com/tangeai/xiaotai/releases)，下载名称以目标板卡 ID 开头的最新发布包。不要下载其他开发板的包，即使它们使用相同芯片。解压后检查 `MANIFEST.json`（发布包清单）中的开发板型号、源码版本、SHA-256 校验值和烧录地址，再按下面的方法写入。

### ESP32 开发板

ESP32 发布包包含多个烧录文件。使用 Chrome 或 Edge 打开[乐鑫官方 ESP Web Tool](https://espressif.github.io/esptool-js/)，连接开发板串口，然后按照 `MANIFEST.json` 的 `flash_files` 逐项选择文件并填写地址。不能只写入应用镜像，也不要自行猜测地址。Safari 不支持该工具。

如果电脑已经安装 ESP-IDF（乐鑫物联网开发框架），也可以使用发布包记录的参数，通过 `esptool.py` 烧录。各开发板的下载模式、串口连接和操作步骤见上表中的“开发板指南”。

### BK7258 开发板

下载 `lckfb-bk7258-` 开头的发布包，使用包内的 `all-app.bin`，从地址 `0x0` 完整烧录。`app_pack.rbl` 是 OTA（无线升级）镜像，不能代替首次完整烧录。烧录工具、下载模式和串口设置见[立创·实战派 BK7258 烧录指南](docs/boards/lckfb-bk7258/README.md#烧录)。

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

需要重新配网或更换绑定账号时，按对应开发板指南分别执行 `wifi-clear` 或 `tirtc-clear`，
不要使用全片擦除。遇到音视频问题时，应记录板卡 ID、固件版本、问题方向和完整日志，
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

ESP-IDF 和 Beken 仍是两个独立 SDK（软件开发工具包）工程。统一入口只负责选板和分发，
不会把两套构建系统混在一起。开发和交付要求见[参与开发](CONTRIBUTING.md)。

## 下一步

- 想了解功能怎样实现：阅读[架构与二次开发](ARCHITECTURE.md)。
- 想修改代码或新增开发板：阅读[参与开发](CONTRIBUTING.md)。
- 想查询某块板的规格、原理图和依赖：打开[开发板目录](docs/boards/README.md)。
- 想查全部产品与工程文档：打开[完整文档](docs/README.md)。

本项目自有源码和文档采用 [MIT License](LICENSE)。第三方 SDK、预编译库、模型、字体和媒体资源仍遵守各自许可证，详见[第三方清单](THIRD_PARTY.md)。
