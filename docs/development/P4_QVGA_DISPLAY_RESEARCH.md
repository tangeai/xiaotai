# ESP32-P4 设备互呼 QVGA 下行转换资料

build.102 的实板日志显示：320×240 下行输入与解码约 13.6–14.3 fps，转换和呈现仅约 5.2–5.6 fps；`engine=software`、`ppa=0`，转换平均 161–170 ms，峰值 223 ms。该段没有解码失败或传输丢帧。低呈现率发生在本机转换与画布写入阶段，尚不能精确区分颜色计算、PSRAM 访问及任务调度耗时。代码见 [`video_frame_converter.c`](../../platforms/esp-idf/waveshare_p4/video_frame_converter.c)。

## 官方资料

- [ESP Image Effects 1.1.0](https://components.espressif.com/components/espressif/esp_image_effects/versions/1.1.0/readme?language=en) 支持 I420 转 RGB565 大端/小端、BT.601 与任意分辨率。其[官方示例](https://github.com/espressif/esp-adf-libs/blob/master/esp_image_effects/test_app/main/color_convert.c)使用 `open`、`process`、`close` 接口。
- [Espressif 1.2.0 更新记录](https://raw.githubusercontent.com/espressif/esp-adf-libs/master/esp_image_effects/CHANGELOG.md)说明新增按 ESP32-P4 芯片修订选择预编译库，并启用 P4 的 PIE 加速。本工程尝试 1.2.0 时正确选择 `esp32p4_less_v3`，但完整工程链接器丢弃内部 SRAM `.sdata` 段而失败；build.105 继续使用可构建的 1.1.0，不把未验证的库升级交给实板。
- [Espressif 的 P4 性能表](https://github.com/espressif/esp-adf-libs/blob/master/esp_image_effects/doc/PERFORMANCE_ESP32P4.md)仅列出 1920×1080 I420→RGB565：优化版 18.1374 fps、普通 C 版 4.5326 fps。没有 QVGA 或同时通话、LCD、摄像头负载的数据，不能据此预测本机帧率。
- [ESP-IDF 5.5.5 PPA 文档](https://docs.espressif.com/projects/esp-idf/en/v5.5.5/esp32p4/api-reference/peripherals/ppa.html)说明阻塞事务等待完成，SRM 由客户端提交；图像位于 PSRAM 时，带宽竞争会显著影响性能。双线性缩放也可能使边缘出现彩边及对比度损失。
- 同一 [PPA 文档](https://docs.espressif.com/projects/esp-idf/en/v5.5.5/esp32p4/api-reference/peripherals/ppa.html)要求输出地址和长度按缓存行对齐。[驱动代码](https://github.com/espressif/esp-idf/blob/v5.5.5/components/esp_driver_ppa/src/ppa_core.c)显示 SRM 请求经队列调度，因此原 PPA 阻塞耗时可能包含等待，现有日志不能精确拆分。
- [RGB LCD 文档](https://docs.espressif.com/projects/esp-idf/en/v5.5/esp32p4/api-reference/peripherals/lcd/rgb_lcd.html)说明 PSRAM 帧缓冲可由 EDMA 读取，CPU 与 EDMA 访问会争用带宽。这是可能的耗时来源，需要实测，不能直接归因为本次卡顿。

## 对当前工程的建议

1. 320×240、0° 下行用 `esp_imgfx_color_convert_process` 批量转为 RGB565，按原尺寸放在 640×480 画布的 `(160,120)`，不放大；旋转及组件失败保留原软件路径。build.103 已实现该试验。
2. 在实板分别测颜色转换、画布清空与复制、排队、最终呈现节拍，并用测试图检查 RGB565 字节顺序。单个 `cvt` 平均值不能分辨各步骤。
3. 若批量转色足够快而画布操作仍慢，再研究局部区域显示或持久黑边；先核对显示缓冲所有权、LVGL 刷新和缓存一致性，不能直接跳过清屏。
4. 若 `color` 是主要耗时，检查转换输入/输出是否位于 PSRAM、任务是否被抢占。build.105 减少了每帧中央区域的冗余清零，`clear` 的变化可以从分段日志判断。1.2.0 升级须先解决链接内存布局，不能仅凭官方独立基准判断收益。
5. 再试验 PPA 以 1:1 比例将 QVGA YUV420 转成 QVGA RGB565，分别记录 I420 打包、PPA 等待/处理和字节交换；同时检查上行缩放是否因共用 PPA 而退步。当前软件 H264 解码器只支持 I420 输出，不能直接跳过颜色空间转换。

日志中的 `audio-tx=0`、`audio-rx=0` 是独立观察，不能由视频颜色转换解释；该段也不能证明音频链路正常。
