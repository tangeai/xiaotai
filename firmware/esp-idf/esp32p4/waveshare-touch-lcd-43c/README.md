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

- `main/application/`：业务会话和媒体资源所有权；
- `main/protocols/tirtc/`：TiRTC 连接、订阅和媒体收发；
- `main/services/`：H5、AI、设备呼叫和微信 VoIP；
- `main/media/`：摄像头、编码和显示媒体链路；
- `main/drivers/`：显示、触摸、音频和摄像头驱动；
- `hardware-ir.json`：硬件事实、来源和证据等级。

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

板卡现有产品功能已经由项目方在实板验证。源码中的构建合同和主机测试用于防止接口回退，
不能替代每次固件改动后的实板回归。

TODO：下一次实板验证时，补充微信 VoIP 流 0 修正后的主叫、被叫、上行首包和双向通话日志。
该证据补充不阻塞当前源码提交。
