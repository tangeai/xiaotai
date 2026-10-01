# ALIENTEK ATK-DNESP32S3

> 板卡照片待补。请以 ATK-DNESP32S3 型号和 PCB（印刷电路板）V1.4 丝印核对硬件。

完整的环境、构建、烧录和验证步骤见
[板卡使用说明](../../../docs/boards/alientek-atk-dnesp32s3/README.md)。本页只记录板级代码职责。

## 构建与烧录

    python3 tools/build.py --board alientek-atk-dnesp32s3
    cd firmware/esp-idf/esp32s3/atk-dnesp32s3
    idf.py -p <串口> flash monitor

首次操作和已知限制见[工程说明](../../../firmware/esp-idf/esp32s3/atk-dnesp32s3/README.md)。

构建入口是 `firmware/esp-idf/esp32s3/atk-dnesp32s3`。构建工具从 `board.json`
读取该路径，其他脚本不应重复写死。

板级代码的职责如下：

- `board_config.h` 保存接线和扩展器配置；
- `atk_board.c` 实现板级音频和摄像头操作；
- `starter_media` 处理媒体队列和 TiRTC（实时音视频软件开发工具包）数据帧；
- `hardware-ir.json` 保存 Hardware IR（硬件信息记录）及证据来源。

产品媒体代码不能直接操作引脚、音频芯片寄存器或摄像头上电时序。
