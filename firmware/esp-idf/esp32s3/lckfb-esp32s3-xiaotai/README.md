# lckfb_esp32s3_xiaotai

本目录是立创·实战派 ESP32-S3（PCB V1.0.1，模组 `ESP32-S3-WROOM-1-N16R8`）
的固件工程，使用 ESP-IDF 5.5.5、TiRTC SDK 2.5.0 和 LVGL 8.3.11。
支持 AI 语音对话、空闲声音识别与结果播报、设备及微信语音通话，以及网页实时查看与双向对讲。
设备和微信通话仅支持音频；摄像头用于网页实时查看。

首次使用请阅读[开发板指南](../../../../docs/boards/lckfb-esp32s3/README.md)，其中包含硬件规格、
依赖安装、烧录、配网、绑定及排查步骤。本页用于定位工程入口和开发约束。

## 构建和烧录

安装板卡指南指定的工具链后，在仓库根目录执行：

```bash
. tools/activate_esp_idf.sh
idf.py --version
python3 tools/build.py --board lckfb-esp32s3
```

确认目标串口后，在本工程目录烧录并查看日志：

```bash
idf.py -p <SERIAL_PORT> flash monitor
```

原生产物保留在 `build/`，应用为 `build/lckfb_esp32s3_xiaotai.bin`。
版本、构建号和打包规则见[固件版本与发布](../../../../CONTRIBUTING.md#固件版本与发布)。
不要复用其他机器生成的 `build/` 或跳过原生构建中的硬件、媒体与内存检查。

## 产品交互

`B/BOOT` 键在空闲时单击启动 AI，再单击结束；来电时单击接听，通话中单击挂断。
双击用于拒接来电，或在空闲时呼叫平台顺序中的首个微信联系人；没有微信联系人时显示添加引导。
`R/RESET` 是硬件复位键。按住 BOOT 再复位会进入下载模式。

来电先提示，用户接听后才停止当前 AI、网页对讲或房间媒体并切换到通话。
已有待接听或一对一通话时，后到呼叫返回忙线。打开菜单和查看运行状态不结束 AI；
进入普通功能页时按产品规则结束会话。完整行为见[交互规范](../../../../docs/product/PRODUCT_INTERACTION_PROFILES.md)。

首页声音识别、字幕、表情、联系人和设置遵循公共产品规则。当前入口和操作见
[首次使用](../../../../docs/boards/lckfb-esp32s3/README.md#首次使用)，不在工程文档重复维护界面细节。

## 媒体与硬件边界

当前 AI 上行为 A-law / 16 kHz，下行为 Opus / 16 kHz；网页对讲、设备通话和微信通话
使用 A-law / 8 kHz。流编号、帧长、缓冲与视频参数以[音视频合同](../../../../docs/product/MEDIA_CONTRACT.md)为准。

摄像头驱动支持资料记载的 GC0308 和实机探测到的 GC2145；后者的器件编号为 `0x2145`。
其他传感器仍拒绝接入。具体板卡事实及证据等级保存在 `hardware-ir.json`，不能把配置值当作实测结果。

ES7210 采集与 ES8311 播放使用 I2S0 配对通道。MIC1 为近端麦克风，MIC3 为硬件回采参考，
进入 AEC（声学回声消除）处理。音频采集、播放、摄像头与电源时序由板级媒体适配器负责；
TiRTC 回调只交付事件或复制媒体数据，不执行阻塞网络请求和媒体销毁。

## 代码入口

以下路径相对仓库根目录。工程内的组件注册文件以各自 `CMakeLists.txt` 为准。

| 路径 | 职责 |
|---|---|
| `platforms/esp-idf/main/` | 共享启动与模块组装 |
| `platforms/esp-idf/components/starter_runtime/` | 会话协调、媒体所有权与超时 |
| `platforms/esp-idf/components/starter_tirtc/` | TiRTC 类型、协议与回调适配 |
| `platforms/esp-idf/components/starter_product/` | 共享界面与产品状态呈现 |
| `platforms/esp-idf/components/starter_media_common/` | 媒体合同、AEC、重采样与预录音 |
| `platforms/esp-idf/components/platform_client/` | 服务发现与平台消息 |
| `boards/lckfb/esp32s3/` | 板级音频、摄像头、显示、触摸及引脚适配 |
| `firmware/esp-idf/esp32s3/lckfb-esp32s3-xiaotai/components/starter_media/` | 本工程媒体任务与硬件组合 |
| `product/` | 平台无关的产品状态与协议 |

## 调试与验证

串口 `version` 查看固件身份，`status` 查看平台、消息通道、会话和媒体计数。
`ai-start` / `ai-stop` 可用于 AI 会话调试。配网、绑定清理与恢复步骤见板卡指南，
不要把设备凭证或无线网络密码保存到日志、脚本和版本库。

文档和代码修改后，从仓库根目录运行 `bash tools/check.sh`。涉及固件时还需完整构建；
涉及交互或媒体时，按[测试说明](../../../../tests/README.md)验证配网绑定、双向声音、
来电接听、挂断重拨及业务切换。当前发布版本和验证范围见[固件下载](../../../../docs/downloads.md#lckfb-esp32s3)。

构建通过不能证明声学效果、弱网恢复或长期稳定性。实板结果须绑定准确的固件版本、
哈希和测试条件；摄像头型号差异及待验证项见[板卡验证状态](../../../../docs/boards/lckfb-esp32s3/README.md#验证状态与已知限制)。
