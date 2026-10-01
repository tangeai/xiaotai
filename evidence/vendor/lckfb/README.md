# 立创实战派 ESP32-S3 官方原理图证据

来源页：<https://wiki.lckfb.com/zh-hans/szpi-esp32s3/open-source-hardware/>

抓取日期：2026-08-27。来源页把“开发板原理图”发布为三张 PNG，而非 PDF。图框标注设计 `ESP32-S3-V1_0_1 / XD-ESP32S3-V1_0_1`、版本 `V1.0.1`。这只能证明官方原理图版本，不能替代对用户手中 PCB 丝印版本的确认。

| 文件 | 内容 | SHA-256 |
|---|---|---|
| `schematic-1.png` | ESP32-S3 模组、按键、I2C、TF 卡、扩展口、QMI8658、PCA9557 | `0b2e6e5b46b9c0e6f17248df62a1481172be28e7261f5fa62c22aa1411230b0d` |
| `schematic-2.png` | USB、DVP 摄像头、触摸、TFT、电源 | `2d485448ae887fc285fccbf22486740e49328d9995ff149cafb8355a851cc333` |
| `schematic-3.png` | ES7210、ES8311、麦克风、NS4150B、扬声器 | `82215b9d8584d0124488b9e9c990b54282f0d77b0ec972fccc522c3b68301ad2` |

相关官方例程文档：

- 摄像头：<https://wiki.lckfb.com/zh-hans/szpi-esp32s3/beginner/camera.html>
- ES7210 输入：<https://wiki.lckfb.com/zh-hans/szpi-esp32s3/beginner/audio-input-es7210.html>
- ES8311 输出：<https://wiki.lckfb.com/zh-hans/szpi-esp32s3/beginner/audio-output-es8311.html>
- 语音识别/AEC 参考通道：<https://wiki.lckfb.com/zh-hans/szpi-esp32s3/beginner/voice-recognition.html>

官方软件页只提供未固定版本的百度网盘分享，没有可审计的 commit/release：<https://wiki.lckfb.com/zh-hans/szpi-esp32s3/open-source-software/>
