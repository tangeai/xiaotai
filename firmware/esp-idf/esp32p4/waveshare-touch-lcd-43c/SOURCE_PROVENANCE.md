# 源码与依赖来源

本文只记录当前工程可复核的来源，不记录版本迁移过程。临时调查材料和历史报告仅在
开发者本地保存，不属于项目文档，也不会提交到仓库。

## 板卡资料

- 目标型号：`ESP32-P4-WIFI6-Touch-LCD-4.3`，商品编号 33875，PCB V1.0；
- 官方资料入口：<https://docs.waveshare.net/ESP32-P4-WIFI6-Touch-LCD-4.3/>；
- 官方参考仓库：<https://github.com/waveshareteam/ESP32-P4-WIFI6-Touch-LCD-4.3>；
- 本地核对的参考提交：`74d1a81`；
- 板级 BSP（板级支持包）：`waveshare/esp32_p4_wifi6_touch_lcd_4_3` v1.0.1。

原理图文件、校验值和实物识别方法见
[开发板使用说明](../../../../docs/boards/waveshare-esp32p4-touch-lcd-43c-v10/README.md)。
引脚、器件和证据等级见 `hardware-ir.json`。

## 构建依赖

- ESP-IDF 5.5.4；
- ESP32-P4 版 TiRTC SDK，身份和校验值见 `components/tirtc_sdk/VERSION.md` 与
  `components/tirtc_sdk/SHA256SUMS.txt`；
- 其他 ESP-IDF 组件的解析版本见 `dependencies.lock`。

共享产品逻辑来自仓库根目录的 `product/`，ESP-IDF 平台适配来自
`platforms/esp-idf/`。本地参考资料只用于核对，不参与发布包。
