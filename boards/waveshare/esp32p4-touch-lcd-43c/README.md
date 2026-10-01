# Waveshare ESP32-P4-WIFI6-Touch-LCD-4.3

> 板卡照片待补。请以完整型号和 PCB（印刷电路板）V1.0 丝印核对硬件。

完整的环境、构建、烧录和验证步骤见
[板卡使用说明](../../../docs/boards/waveshare-esp32p4-touch-lcd-43c-v10/README.md)。本页只记录板级代码职责。

## 构建与烧录

    python3 tools/build.py --board waveshare-esp32p4-touch-lcd-43c-v10
    cd firmware/esp-idf/esp32p4/waveshare-touch-lcd-43c
    idf.py -p <串口> flash monitor

首次操作和已知限制见[工程说明](../../../firmware/esp-idf/esp32p4/waveshare-touch-lcd-43c/README.md)。

构建入口是 `firmware/esp-idf/esp32p4/waveshare-touch-lcd-43c`。
准确的屏幕、触摸、音频、摄像头和无线连接事实记录在 `hardware-ir.json` 中。

`board_config.h` 保存本板独有的屏幕方向、音频通道和 BSP（板级支持包）选择。
