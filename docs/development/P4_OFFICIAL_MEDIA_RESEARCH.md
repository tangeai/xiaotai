# ESP32-P4 双向视频性能：官方资料与本板实测

研究范围：Waveshare ESP32-P4 Touch LCD 4.3C、OV5647、ESP-IDF 5.5.5，设备互呼的双向视频。这里把 Espressif 的规格、驱动实现、本板日志和待验证的推断分开。**单一外设的标称帧率不能当作摄像头、显示、音频和 Wi-Fi 同时工作时的整机帧率。**

## 已确认的硬件和驱动事实

1. P4 有专用 H.264 **编码器**；Espressif 的组件文档称其单独编码 1080p 可超过 30 fps，软件解码器则来自 tinyH264。[ESP-H264 官方组件](https://github.com/espressif/esp-h264-component/blob/master/esp_h264/README.md)。这说明当前 QVGA 上行帧率不应先归咎于 H.264 编码硬件，但不证明双向通话也可达到该数字。
2. PPA 的处理时间随处理块的数据量增长；图像在 PSRAM 且多个外设同时读写 PSRAM 时，性能会大幅下降。[ESP-IDF 5.5.5 PPA 性能说明](https://docs.espressif.com/projects/esp-idf/en/v5.5.5/esp32p4/api-reference/peripherals/ppa.html#performance-overview)。当前项目调用 `PPA_TRANS_MODE_BLOCKING`（[`video_frame_converter.c`](../../platforms/esp-idf/waveshare_p4/video_frame_converter.c)），而 IDF 的阻塞实现还会等待 PPA 引擎及 DMA2D 事务队列和完成信号；日志中的 `hardware_wait` 是**整次调用的墙上时间**，不能直接解释为像素计算耗时。[IDF `ppa_core.c`](https://github.com/espressif/esp-idf/blob/v5.5.5/components/esp_driver_ppa/src/ppa_core.c#L415-L471)。
3. P4 只有一套 JPEG 编解码引擎，某一时刻只能编码或解码。[ESP-IDF 5.5.5 JPEG 说明](https://docs.espressif.com/projects/esp-idf/en/v5.5.5/esp32p4/api-reference/peripherals/jpeg.html)。IDF 编码和解码入口共用 `codec_mutex`，所以双向 MJPEG 必然串行访问 JPEG 硬件。[编码入口](https://github.com/espressif/esp-idf/blob/v5.5.5/components/esp_driver_jpeg/jpeg_encode.c#L176-L183)、[解码入口](https://github.com/espressif/esp-idf/blob/v5.5.5/components/esp_driver_jpeg/jpeg_decode.c#L270-L278)、[共享句柄](https://github.com/espressif/esp-idf/blob/v5.5.5/components/esp_driver_jpeg/jpeg_common.c#L36-L64)。官方 JPEG 性能表是在 360 MHz CPU、200 MHz PSRAM、**仅运行 JPEG** 的条件下测得，不能与本板通话日志直接比较。[官方测试条件](https://docs.espressif.com/projects/esp-idf/en/v5.5.5/esp32p4/api-reference/peripherals/jpeg.html#performance-overview)。
4. 当前固件的**最低支持修订版本**配置是 `CONFIG_ESP32P4_REV_MIN_FULL=100`，并选择了 `CONFIG_ESP32P4_SELECTS_REV_LESS_V3=y`；这不等于已测得实物芯片的修订版本。CPU 360 MHz、PSRAM 200 MHz（[`sdkconfig`](../../firmware/esp-idf/esp32p4/waveshare-touch-lcd-43c/sdkconfig)）。早期 P4 的 JPEG 硬件**不支持 YUV420/YUV444 作为编码输入**；IDF 5.5.5 在 `jpeg_encode.c` 中根据当前编译配置屏蔽这两个分支。UYVY/YUV422、RGB565、RGB888 则可用。[IDF 编码格式分支](https://github.com/espressif/esp-idf/blob/v5.5.5/components/esp_driver_jpeg/jpeg_encode.c#L193-L223)、[Espressif `esp_video` 格式表及 ECO3 注释](https://github.com/espressif/esp-video-components/blob/master/esp_video/README.md#video-device)。因此不能把相机当前的 packed YUV420 直接提交给**当前固件**的 JPEG 编码器。**实板补充：**IDF 参数验证虽允许 YUV422 输入配 YUV420 JPEG，DMA2D 的 `enc_hb_tbl` 对此组合为 0；build.111 连续超时。YUV422 输入配 YUV422 JPEG 的表项为 64，build.112 改用此组合。[IDF DMA2D 参数表](https://github.com/espressif/esp-idf/blob/v5.5.5/components/esp_driver_jpeg/jpeg_param.c#L56-L63)。
5. JPEG 编码器的 YUV422 输入布局是 UYVY：相邻两个像素为 `U,Y0,V,Y1`。本板当前采集缓冲则是逐行交错 `OUYY/EVYY` 的 packed YUV420；两者不能直接互传。[官方 JPEG 像素布局](https://docs.espressif.com/projects/esp-idf/en/v5.5.5/esp32p4/api-reference/peripherals/jpeg.html#pixel-storage-layout-for-different-color-formats)、[本项目半缩小实现](../../platforms/esp-idf/waveshare_p4/video_yuv420_scaler.c)。
6. 官方 `esp_video` 把 P4 的 MIPI-CSI/ISP、JPEG 编解码和 H.264 编码都暴露为不同 V4L2 设备；其文档列出 ISP 的 RAW 转 RGB/YUV 和 JPEG/H.264 的输入格式，但未给出 ISP 对本项目 OV5647 RAW10 采集做 1280×960→640×480 缩放的现成接口或性能承诺。应以实际驱动能力验证，不能假定 ISP 能替代 PPA。[Espressif `esp_video` 设备表](https://github.com/espressif/esp-video-components/blob/master/esp_video/README.md#video-device)、[ESP-IDF 5.5.5 ISP API](https://docs.espressif.com/projects/esp-idf/en/v5.5.5/esp32p4/api-reference/peripherals/isp.html)。

## 本板测量与可支持的结论

以下数字来自用户提供的串口日志，不是 Espressif 的硬件保证。

| 固件及模式 | 上行主要阶段 | 实际上行 | 解释 |
| --- | --- | --- | --- |
| build.102/103，H.264 QVGA 15 fps | 1280×960→320×240 约 64–67 ms；H.264 硬编约 2–3 ms | 约 13.4–14.0 fps | 预处理已接近每帧 66.7 ms 的预算，硬编不是主耗时。|
| build.108，MJPEG VGA 10 fps | PPA 等待平均约 213–256 ms；JPEG 编码约 55–91 ms；同步提交约 64–106 ms | 约 2.1–2.5 fps | `budget-drop=0`，不是码率预算限速；各段串行，且双向 JPEG、PPA、显示争用可能叠加。仅凭这些计时不能拆出每个硬件的纯运行时间。|
| build.109，MJPEG VGA 10 fps，用户新测 | packed YUV→RGB565 的 CPU 路径约 122 ms；JPEG 编码约 7 ms；提交约 2 ms | 本端上行约 7.5 fps；本端接收对端视频约 4.2 fps | 绕过 PPA 后明显改善，进一步表明旧 `hardware_wait` 不是 JPEG 硬编性能本身；CPU 转换仍超 100 ms 帧周期。本端 `rx/decoded/presented` 增量基本一致，下行帧率低须先核对对端实际发送帧率。|
| build.112，MJPEG VGA 8 fps，UYVY/YUV422 | 全幅半缩约 128–136 ms；JPEG 编码约 8–9 ms；提交约 4–6 ms | 本端上行约 6.3–6.6 fps、2.67–3.30 Mbit/s；下行约 8 fps 接收、约 7.2 fps 解码、约 4.8 fps 交付显示 | build.111 的 DMA2D 超时未再出现，解码失败和音频接收缓冲溢出为零；转换仍超过 125 ms 帧周期，颜色及面板实际刷新未验收。|

当前全幅 1280×960 采集是为了避开已经在本板观察到的 OV5647 原生 VGA 彩边。彩边的成因仍未被证明，可能涉及传感器窗口/抽样与 Bayer 相位或 ISP 配置；不应在未抓 RAW 图和验证模式寄存器前宣称“P4 原生 VGA 不可用”。[本项目原生 VGA 试验](../hardware/P4_OV5647_NATIVE_VGA.md)、[Espressif OV5647 模式配置](https://github.com/espressif/esp-video-components/tree/master/esp_cam_sensor/sensors/ov5647)。

build.112 后续约 15 分钟日志显示，JPEG 转换时间随场景约 130–150 ms，
上行多为 5.6–6.4 fps；积压到约 75 KiB 后，保护策略停止本端视频上行，
音频及下行继续，人工重开摄像头后上行恢复。四向旋转均有几何日志，
当前 MJPEG `cover` 渲染在 90°/270° 中心裁切；这不能代替实屏观感验收。

## 按证据排序的后续试验

1. **先做单向隔离基线。** 固定同一光照、静止/运动场景、芯片频率和帧大小，依次测量：采集+转换；再加编码；再加提交；最后启用对端视频、显示、音频。每段记录均值、P95、最大值、有效帧率、PSRAM/DMA 占用、JPEG 编解码次数及 `TiRtcSendVideoStream` 调用时间。这样能把本端转换成本与双向资源争用分开。
2. **复核 UYVY 路径的颜色与时间。** build.111 已将 1280×960 packed YUV420 半缩成 640×480 UYVY，每组输出 `U,Y0,V,Y1`，亮度按 2×2 平均，色度按相邻源块平均。它绕开 PPA 和逐像素 YUV→RGB 矩阵计算，但以 YUV420 子采样编码持续触发 DMA2D 超时；build.112 改用驱动表支持的 `JPEG_ENCODE_IN_FORMAT_YUV422`、`JPEG_DOWN_SAMPLING_YUV422`，实板已能连续编码发送。转换均值约 128–136 ms，**颜色是否正常仍须同场景画面对照**。
3. **分离 JPEG 硬件等待。** 在调用 `jpeg_encoder_process` / `jpeg_decoder_process` 前后记录等待和处理时间；必要时仅在调试构建中为共用 `codec_mutex` 和 DMA2D 队列加时间戳。若双向 JPEG 下编码 7 ms 上升数十毫秒，则考虑限制下行处理频率、只保留最新帧，或测试协议允许的非对称编解码。不能让生产代码长期依赖修改 IDF 内部驱动。
4. **检查同步发送的排队。** `runtime.video_cb` 在相机任务内调用，下游返回前下一帧不能开始；build.108 的 64–106 ms 提交时间足以消耗 10 fps 帧周期。只有确认 TiRTC 对输入缓冲的生命周期后，才能用双缓冲及有界发送任务解耦；队列满时丢完整的旧帧，不积压过时画面。Espressif 的 [RTSP 示例原则](https://docs.espressif.com/projects/esp-vision/en/latest/esp32p4/concepts/codec-streaming.html#network-streaming-rtsp)也是慢客户端时丢完整帧。
5. **独立核查接收端。** 对端约 4 fps 需同时采 `rx/decoded/presented`、JPEG 解码墙上时间、呈现队列深度及屏幕刷新时长。若接收仅 4 fps，先查传输和发送；若接收约 8–10 fps 而呈现 4 fps，则查 JPEG 共用引擎和显示刷新，避免再改上行缩放。
6. **长期方案是修正低分辨率采集。** 若能用官方 OV5647 模式/寄存器和 RAW/ISP 对照修复原生 VGA 彩边，可直接输出编码器需要的尺寸，减少整帧读写和缓冲。修复前保留全幅采集及经过验证的缩小路径。

不要把 JPEG 单测 300 fps、H.264 单测 1080p30 或“PSRAM 空闲还有数 MB”当成双向通话性能的证明；这里约束的是共享硬件、内存**带宽**和同步任务链路，而不只是可用容量。
