# 小钛 ESP32-P4 工程

当前应用工程使用 **ESP-IDF v5.5.5**；先在仓库根目录执行
`. tools/activate_esp_idf.sh`，再用 `idf.py --version` 核对。


本目录是 `ESP32-P4-WIFI6-Touch-LCD-4.3` 的 ESP-IDF 工程，供二次开发者定位
构建入口与媒体实现。规格、依赖、编译、烧录和硬件验证步骤请从
[开发板使用说明](../../../../docs/boards/waveshare-esp32p4-touch-lcd-43c-v10/README.md) 开始。
本轮修改、新增功能、验证结果和未完成的实板检查见[工程变更记录](CHANGELOG.md)。

## 工程边界

本工程保留 ESP32-P4 专用的应用、驱动和第三方组件。可复用的板级实现位于
`platforms/esp-idf/waveshare_p4/`，硬件配置位于
`boards/waveshare/esp32p4-touch-lcd-43c/`。产品行为通过共享接口接入，板级代码负责
GPIO（通用输入输出引脚）、总线、时钟、DMA（直接内存访问）、电源时序和外设。

主要入口如下：

- 仓库 `platforms/esp-idf/main/`：共享启动入口，由本工程 `main/CMakeLists.txt` 引入；
- 仓库 `platforms/esp-idf/components/starter_runtime/`：业务会话和媒体所有权；
- 同目录 `starter_tirtc/`、`platform_client/`：TiRTC 与平台信令；
- 同目录 `starter_product/`：产品界面和交互；
- 本工程 `components/starter_media/`、`components/p4_hardware/`：P4 媒体适配；
- 本工程 `main/media/`、`main/drivers/display/`：摄像头编码与显示实现；
- `hardware-ir.json`：硬件事实、来源和证据等级。

源文件是否生效以组件的 `CMakeLists.txt` 为准。旧 `main/application/`、
`main/protocols/`、`main/ui/` 不作为当前产品入口。详细路径见[媒体架构](docs/P4_MEDIA_ARCHITECTURE.md)。

## 媒体约定

H5 使用音频流 10/14 和视频流 11/15；微信 VoIP 使用音频流 0、视频流 1。
详细的编码格式、采样率、包长、分辨率、帧率和 AEC（声学回声消除）处理见
[音视频参数与媒体处理](../../../../docs/product/MEDIA_CONTRACT.md)。

## 编译与检查

从仓库根目录执行：

```bash
python3 tools/build.py --validate
python3 tools/build.py --board waveshare-esp32p4-touch-lcd-43c-v10
bash tools/check.sh
```

原生产物保留在工程 `build/` 下，应用为 `build/xiaotai_esp32p4.bin`。
打包与导出按[固件版本与发布](../../../../CONTRIBUTING.md#固件版本与发布)执行。
工程内的 `build/`、日志和临时报告不提交。

## 证据与待办

当前媒体参数以 `main/media/media_tuning.h` 和
[音视频合同](../../../../docs/product/MEDIA_CONTRACT.md)为准：H5 上行 H.264
1280×960/20 fps/3 Mbit/s；设备互呼上行 MJPEG
640×480/8 fps/质量 75/4 Mbit/s 发送预算，下行 MJPEG；微信 VoIP 上行
H.264 320×240/15 fps/200 kbit/s，下行 MJPEG。设备互呼从 OV5647 的
1280×960 全幅画面半缩为 UYVY，再用 YUV422 JPEG 编码；原生 VGA 采集仍关闭。

build.112（ELF SHA-256 前缀 `a14250e62`）的设备互呼日志确认 JPEG 能持续编码、
发送和解码，没有 build.111 的 DMA2D 超时。上行实测约 6.3–6.6 fps、
2.67–3.30 Mbit/s，下行约 8 fps 接收、约 7.2 fps 解码、约 4.8 fps
交付显示；音频接收缓冲溢出为零。`presented` 不等于面板实际刷新率。
约 15 分钟的同版日志还显示：低档持续积压后本端视频上行停发，音频和下行继续，
人工重开摄像头后上行恢复。该版四向旋转有几何日志，90°/270° 曾按 `cover`
策略中心裁切；当前代码已改为设备互呼 `contain`、微信 `cover`，仍待实屏验收。
画面颜色、旋转视觉效果、H5/微信和长时间稳定性尚未验收。

历史实验与阶段计时见[官方资料和本板测量](../../../../docs/development/P4_OFFICIAL_MEDIA_RESEARCH.md)、
[通话诊断记录](../../../../docs/development/MEDIA_PIPELINE_DIAGNOSTICS.md)及
[视频性能记录](../../../../docs/product/P4_VIDEO_PERFORMANCE.md)。修改媒体配置后
仍需双机实测双向音视频、挂断重拨、旋转、弱网恢复和资源释放。
