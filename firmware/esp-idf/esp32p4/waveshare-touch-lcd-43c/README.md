# 小钛 ESP32-P4 工程

本目录是 `ESP32-P4-WIFI6-Touch-LCD-4.3` 的 ESP-IDF 工程。面向使用者的规格、
依赖、编译、烧录和首次使用步骤，请从
[开发板使用说明](../../../../docs/boards/waveshare-esp32p4-touch-lcd-43c-v10/README.md) 开始。

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

生成物由构建脚本统一复制到 `output/`。工程内的 `build/`、日志和临时报告不提交。

## 证据与待办

已有实板结论的适用范围见开发板指南及[视频性能记录](../../../../docs/product/P4_VIDEO_PERFORMANCE.md)。
设备互呼当前以 5 fps 为基线，不声明 8 fps 已通过稳定性验收。主机测试防止接口回退，
不能替代新固件的双向音视频、四向旋转、连续挂断重拨和弱网回归。
