# 立创·实战派 ESP32-S3

> 板卡照片待补。本板使用 `ESP32-S3-WROOM-1-N16R8` 模组；请以
> SZPI ESP32-S3 AIROBOT 和 PCB（印刷电路板）V1.0.1 丝印核对实物。

完整的环境、构建、烧录和验证步骤见
[板卡使用说明](../../../docs/boards/lckfb-esp32s3/README.md)。本页只记录板级代码职责。

## 构建与烧录

    python3 tools/build.py --board lckfb-esp32s3
    cd firmware/esp-idf/esp32s3/lckfb-esp32s3-xiaotai
    idf.py -p <串口> flash monitor

首次操作和已知限制见[工程说明](../../../firmware/esp-idf/esp32s3/lckfb-esp32s3-xiaotai/README.md)。

构建入口是 `firmware/esp-idf/esp32s3/lckfb-esp32s3-xiaotai`。工程使用共享的
ESP-IDF（乐鑫物联网开发框架）平台组件和独立板级适配层。

板级代码的职责如下：

- `board_config.h` 保存接线和 PCA9557 扩展器配置；
- `board_adapter.c` 管理 I²C（芯片间串行总线）、摄像头、功放和音频器件；
- `board_display.c` 管理屏幕、触摸和背光；
- `starter_media` 处理 AEC（声学回声消除）、音视频编码和会话媒体队列。

完整固件和实板产品流程已经跑通。发布前仍需按统一的 HIL（硬件在环测试）格式，
归档固件校验值、测试日期和日志摘要。
