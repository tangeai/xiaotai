# ESP32-S3 内部 SRAM / PSRAM 优化方案

## 结论

当前硬件不是“16 MB PSRAM”，而是 **16 MB Quad SPI Flash + 8 MB Octal PSRAM**。ESP32-S3-WROOM-1-N16R8 的官方规格表也是这一组合；N16R16VA 才是 16 MB PSRAM 的另一个模组型号。当前启动日志的 `SPI Flash Size : 16MB` 和 `Found 8MB PSRAM device` 与 N16R8 一致。[ESP32-S3-WROOM-1/WROOM-1U 数据手册](https://www.espressif.com/sites/default/files/documentation/esp32-s3-wroom-1_wroom-1u_datasheet_en.pdf)

ESP32-S3 芯片有 512 KB 片上 SRAM，另有 16 KB RTC SRAM，但缓存、静态段、系统组件、任务栈和 DMA 会共同占用它，并非全部都能作为普通 8-bit/DMA 堆使用。[ESP32-S3 数据手册](https://documentation.espressif.com/esp32_s3_datasheet_en.pdf)

本项目的 8 MB PSRAM 容量足够。历史故障的本质是：**TLS/TiRTC 启动时内部 SRAM 和 DMA-capable 连续块不足，而不是 PSRAM 总容量不足**。优先方案是把已审计且不执行 NVS/原始 Flash 操作的 UI、媒体、会话、识别栈及工作区放入 PSRAM，同时让 cache-disabled 启动栈、DMA 描述符和安全敏感的 TLS 控制数据保留在内部 SRAM。

## 当前工程基线

> 2026-08-30 更新：下文的 164,527-byte DIRAM、双 24 KiB worker 和按值队列是制定方案时的故障基线。方案已实施：当前全双工 AEC + WDT 公平性 ELF 静态 DIRAM 为 144,231 bytes（42.2%），`starter_start` 已复用为延迟 HTTP worker，媒体/平台队列已改为 PSRAM 固定池 + 1-byte 索引队列，Wi-Fi 静态 RX 为 6。direct camera PSRAM DMA 因精确实机花屏证据被回退到内部 staging DMA。

2026-09-02 的 `.33` 已把 AEC、本地语音和定制 TFLite 唤醒统一接入：`FD_LOW_COST` 算法状态、MIC/reference/clean/8 kHz 工作帧继续优先 PSRAM，只把约 4 KiB 的四槽 I2S 目标留在内部 SRAM；同一份 16 kHz clean frame 非阻塞送入常驻 TFLite 唤醒器和唤醒后的 `mn7_cn` MultiNet7。音频收发、摄像头、LVGL、会话和识别任务栈均使用 PSRAM；调用 NVS/分区读取的 13 KiB `starter_start` 栈仍留在内部 SRAM。

`.24` 真机曾把 24 KiB `starter_start` 栈一并外移，但该任务调用 NVS 和读取模型分区；Flash 操作关闭 cache 时 ESP-IDF 断言 `esp_task_stack_is_sane_cache_disabled()`，设备在联网读取绑定信息时重启。因此 `.33` 保留 cache-safe 内部启动栈，只外移已审计的长期任务。`.33` 真机启动 HIL 中，Wi-Fi 后内部空闲 48,063 bytes、最大连续块 45,056 bytes；释放 20 KiB TiRTC 预留后，TiRTC pre-init 内部空闲 41,403 bytes、最大连续块 32,768 bytes，并在约 6.9 秒收到 `SDK started`，采集窗口内无 alloc failure 或重启。长时 AI/H5/弱网压力仍按本文 HIL 门槛执行。

以下结果基于当前源码、`sdkconfig`、启动日志和现有 ELF：

- `idf.py size`：DIRAM 使用 164,527 / 341,760 字节，其中 `.bss` 44,048 字节、`.data` 31,640 字节、DIRAM `.text` 88,839 字节。
- 启动失败时：`internal-free` 约 35 KB，最大连续内部块仅 18,432 字节；随后硬件 AES 报分配失败。
- PSRAM 已通过 `CONFIG_SPIRAM_USE_MALLOC=y` 加入普通堆；当前阈值为 16,384 字节，即普通 `malloc()` 大于该值时优先 PSRAM，小于等于该值时优先内部 SRAM。这个阈值只是“偏好”，不是能力约束。[ESP-IDF 5.5 外部 RAM](https://docs.espressif.com/projects/esp-idf/en/release-v5.5/esp32s3/api-guides/external-ram.html)
- `CONFIG_SPIRAM_MALLOC_RESERVE_INTERNAL=131072` 已预留 128 KB 给内部/DMA 分配，但普通 `xTaskCreate()` 创建的任务栈也会消耗这个池。
- `CONFIG_MBEDTLS_INTERNAL_MEM_ALLOC=y`，mbedTLS 动态对象全部强制使用内部 SRAM。
- Flash Encryption 当前关闭；因此不应未经安全评审就把全部 TLS 动态对象迁入未加密 PSRAM。

### 已经使用 PSRAM 的对象

- 摄像头两个 RGB565 QVGA 帧缓冲：`153,600 × 2 = 307,200` 字节，显式使用 `CAMERA_FB_IN_PSRAM`。
- `frame2jpg()` 每帧申请 128 KB JPEG 输出缓冲；按当前大块分配策略会优先 PSRAM。官方实现确实每次申请固定 128 KB 后再释放。[Espressif esp32-camera `to_jpg.cpp`](https://github.com/espressif/esp32-camera/blob/master/conversions/to_jpg.cpp)
- TiRTC 256 KB 发送缓存如果 SDK 使用普通大块 `malloc()`，会优先 PSRAM；若 SDK 显式请求内部 capability，则必须依靠运行日志确认，不能仅凭配置推断。

### 主要内部 SRAM 占用

#### 应用任务栈

TLS 启动阶段同时存在的应用自建任务栈合计 **82,944 字节**：

| 任务 | 栈大小 |
|---|---:|
| `starter_start` | 24,576 |
| `platform_http` | 24,576 |
| `starter_session` | 13,312 |
| `board_audio_rx` | 6,144 |
| `board_audio_tx` | 6,144 |
| `board_mjpeg` | 8,192 |

这还不包含 Wi-Fi、TCP/IP、MQTT、TiRTC、console 等系统或第三方任务。ESP-IDF 5.5 明确说明：普通 `xTaskCreate()` 始终从内部内存分配栈；仅打开“允许外部栈”并不会自动迁移。[ESP-IDF 5.5 外部 RAM限制](https://docs.espressif.com/projects/esp-idf/en/release-v5.5/esp32s3/api-guides/external-ram.html#restrictions)

#### 按值复制的队列

使用当前 ELF 的调试类型计算：

- `sizeof(platform_request_t) = 2,196`，深度 4，队列数据区约 **8,784 字节**。
- `sizeof(audio_rx_item_t) = 1,520`，深度 8，队列数据区约 **12,160 字节**。
- `sizeof(runtime_event_t) = 36`，深度 6，仅 216 字节，不是主要问题。

平台请求和音频队列仅数据区就常驻约 **20,944 字节内部 SRAM**，并且生产者/消费者函数还各自在栈上创建完整结构副本。

#### 摄像头 DMA 中转缓冲

当前 `CONFIG_CAMERA_PSRAM_DMA` 关闭，启动日志显示 camera HAL 申请 **30,720 字节内部 DMA 中转缓冲**，再复制到 PSRAM 帧缓冲。官方 camera Kconfig 和 HAL 已支持 ESP32-S3 直接 DMA 到 PSRAM；DMA 描述符仍保留在内部 SRAM。[Espressif esp32-camera `cam_hal.c`](https://github.com/espressif/esp32-camera/blob/master/driver/cam_hal.c)

#### 网络静态 BSS

当前链接结果中，启用官方 external-BSS 选项后有机会迁入 PSRAM 的网络 BSS 约 **14,316 字节**：

- `net80211`: 7,602
- `lwIP`: 4,150
- `wpa_supplicant`: 1,330
- `libpp`: 1,234

ESP-IDF 官方说明 `CONFIG_SPIRAM_ALLOW_BSS_SEG_EXTERNAL_MEMORY` 会把这些库的零初始化段迁入 PSRAM，也允许用 `EXT_RAM_BSS_ATTR` 标记应用自己的安全对象。[ESP-IDF 5.5 External BSS](https://docs.espressif.com/projects/esp-idf/en/release-v5.5/esp32s3/api-guides/external-ram.html#allow-bss-segment-to-be-placed-in-external-memory)

#### Wi-Fi DMA 缓冲

当前 `CONFIG_ESP_WIFI_STATIC_RX_BUFFER_NUM=10`，每个静态 RX DMA 缓冲约 1,600 字节。Espressif 明确给出：从 10 降到 6 可节省 **6,400 字节**，AMPDU 开启时不建议低于 6。[ESP-IDF 5.5 Wi-Fi Buffer Usage](https://docs.espressif.com/projects/esp-idf/en/release-v5.5/esp32s3/api-guides/wifi.html#wi-fi-buffer-usage)

## 根因判断

1. PSRAM 已经用于摄像头帧和 JPEG 大块缓存，不是“完全没用上”。
2. 当前 16 KB 普通分配阈值使大量 2–8 KB 对象仍优先内部 SRAM，但更大的问题是任务栈和 FreeRTOS 队列本来就不会因为该阈值自动进入 PSRAM。
3. `starter_start` 和 `platform_http` 两个 24 KB 栈在 `TiRtcStart()` 握手阶段重叠，造成不必要的启动峰值。
4. camera HAL 在帧缓冲已经位于 PSRAM 的情况下，仍常驻 30,720 字节内部 DMA 中转区。
5. mbedTLS 全部使用内部 SRAM。Espressif 的 HTTPS 示例中，默认 mbedTLS 堆使用约 42,196 字节；打开动态 TX/RX 及握手后释放配置/CA 后约 22,013 字节，但该收益必须在 TiRTC SDK 上单独验证。[ESP-IDF 5.5 mbedTLS 内存优化](https://docs.espressif.com/projects/esp-idf/en/v5.5/esp32s3/api-reference/protocols/mbedtls.html#reducing-heap-usage)
6. AES 报错来自 DMA/对齐辅助分配路径。总 free 大于请求大小并不代表成功；必须同时看 capability 和最大连续块。`heap_caps_get_largest_free_block()` 返回给定 capability 当前可成功申请的最大单块。[ESP-IDF 5.5 Heap Capability API](https://docs.espressif.com/projects/esp-idf/en/release-v5.5/esp32s3/api-reference/system/mem_alloc.html)

## 分阶段实施方案

每阶段只引入一个高风险变量，完成构建和 HIL 对比后再进入下一阶段。

### 阶段 0：先补齐观测，不改变分配策略

1. 注册 `heap_caps_register_failed_alloc_callback()`，记录失败请求的 `size`、`caps` 和分配函数名。
2. 在以下节点打印三类 heap 的 `free / minimum / largest`：
   - `MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT`
   - `MALLOC_CAP_INTERNAL | MALLOC_CAP_DMA`
   - `MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT`
3. 节点至少覆盖：media 后、Wi-Fi 后、platform 后、TiRTC pre-init、pre-start、started、AI token、AI connected、H5 8 fps 运行中、断连后。
4. 为全部应用任务保存 handle，并在任务经历最重路径后读取 `uxTaskGetStackHighWaterMark()`。ESP32-S3 上该值单位是字节。[ESP-IDF 5.5 RAM Usage](https://docs.espressif.com/projects/esp-idf/en/release-v5.5/esp32s3/api-guides/performance/ram-usage.html#determining-stack-size)

这一阶段能确认 AES 失败究竟是 1.6 KB 对齐区、DMA 描述符，还是其他 capability 请求，避免只根据总 free 猜测。

### 阶段 1：摄像头直接 DMA 到 PSRAM

单独打开 `CONFIG_CAMERA_PSRAM_DMA=y`，其他配置不变。

**HIL 结果：不采用。** 精确绑定到 BIN `7eeda2ed…` / ELF `eebcaa452…` 的实机运行成功进入 H5 active，但 GC2145 QVGA RGB565 画面出现严重水平行碎裂。当前恢复驱动默认的内部 DMA 中转路径，仍保留 PSRAM framebuffer。该选择牺牲约 30 KiB 动态内部 SRAM，换取正确帧数据；其余队列、网络 BSS、JPEG 和 HTTP 工作区外移已经为 TLS 留出余量。

预期：释放当前 30,720 字节内部 DMA 中转缓冲，只保留内部 DMA 描述符；每个 PSRAM frame buffer 会增加对齐/半缓冲余量，8 MB PSRAM 足以承担。

验收：

- GC2145 RGB565 QVGA 连续采集无花屏、错行、撕裂和超时。
- H5 MJPEG 8 fps 连续 30 分钟。
- camera frame drop、CPU、Wi-Fi吞吐不能出现明显回退。
- pre-TiRTC 的 DMA largest block 应显著增加；若没有增加，使用 failed-allocation hook 和 heap region 信息继续定位。

### 阶段 2：消除启动阶段的双 24 KB 栈重叠

把平台模块拆成“bootstrap”和“request worker”两个生命周期：

1. `platform_client_start()` 只完成发现、token、MQTT 和信令订阅。
2. 等 TiRTC `on_started` 成功后再创建 `platform_http` 请求 worker 和请求池。
3. AI 入口只有在 platform request worker ready 后才允许启动。
4. 心跳若必须在 TiRTC started 前发送，使用独立的小型机制，不能为了心跳提前常驻 24 KB HTTP 栈。

预期：TiRTC 握手阶段立即减少 `24,576 + 8,784 ≈ 33.4 KB` 内部占用。另一种可选设计是让 `starter_start` 在启动成功后直接转为平台请求循环，从而全生命周期只保留一个网络工作栈。

### 阶段 3：把大队列改成“PSRAM 固定池 + 内部小索引队列”

不采用无界 `malloc()`，保持实时路径有界：

#### 平台请求池

- 初始化时用 `heap_caps_calloc(..., MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT)` 分配 4 个 `platform_request_t` slot。
- FreeRTOS queue 只传 `uint8_t` slot index 或指针。
- 维护 free/ready 两个小队列；队列满时保持现有非阻塞失败语义。
- 回调结束后 slot 必须归还，所有异常路径统一释放所有权。

直接节省约 8,784 字节内部 queue storage，并消除调用者栈上的 2,196 字节大结构，有利于继续缩小 `starter_session` 栈。

#### 音频接收池

- 在 PSRAM 中固定分配 8 个 `audio_rx_item_t`。
- TiRTC 回调只获取空闲 slot、复制 payload、投递索引，不能阻塞。
- 播放任务消费后归还 slot。
- `s_play_stereo` 继续保留内部 SRAM，作为 I2S/DMA 安全缓冲；不要直接把 DMA 描述符或未确认支持的 I2S DMA 数据放入 PSRAM。

直接节省约 12,160 字节内部 queue storage，并降低 TiRTC 回调栈和音频任务栈峰值。

### 阶段 4：PSRAM 工作区和碎片治理

1. 把平台模块的 8 KB HTTP response 从自动栈数组改为串行复用的 PSRAM scratch buffer。
2. AI 事件 JSON、非 DMA 信令副本等大于约 1 KB 的有界临时对象显式使用 `MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT`，不要只依赖全局 malloc 阈值。
3. 用 `frame2jpg_cb()` 把 JPEG 输出收集到一次性预分配的 128 KB PSRAM 缓冲，避免 8 fps 下每秒多次申请/释放 128 KB，降低 PSRAM 碎片和分配抖动。
4. 所有 PSRAM 对象分配后在调试版本用 `esp_ptr_external_ram()` 断言位置。

完成栈上大数组外移并取得 HIL 高水位后，再逐个调整任务栈；每个任务至少保留 2 KB 且不低于实测峰值 25% 的安全余量，以两者较大者为准。2026-09-06 的 `.57` 真机证据表明 `starter_session` 的 13 KiB 栈最低仅余 428 bytes，不能继续缩小；`.58` 已改为 24 KiB PSRAM 栈，构建侧已知调用链 8,416 bytes、静态嵌套余量 16,160 bytes，最终高水位仍待 `.58` HIL。

### 阶段 5：迁移安全的 BSS，并小幅调整 Wi-Fi

1. 单独启用 `CONFIG_SPIRAM_ALLOW_BSS_SEG_EXTERNAL_MEMORY=y`，预期把当前约 14.3 KB 网络 BSS 迁到 PSRAM。
2. 仅对不用于 ISR、DMA、cache-disabled 路径且不含长期密钥的数据添加 `EXT_RAM_BSS_ATTR`。
3. 把 `CONFIG_ESP_WIFI_STATIC_RX_BUFFER_NUM` 与 `RX_BA_WIN` 从 10/6 调至 4/4，静态 RX 相对默认 10 个理论节省 9,600 字节 DMA 内存；保持动态 TX，用弱网和 8 fps 实测验证。
4. 暂时保持 `CONFIG_SPIRAM_TRY_ALLOCATE_WIFI_LWIP` 关闭。ESP-IDF 5.5 开启它会联动 Wi-Fi TX 缓冲和默认窗口配置，不能把它当作无副作用的“一键迁 PSRAM”。

### 阶段 6：TLS 配置作为独立 A/B 实验

前五阶段完成后若 TiRTC 仍缺内存，再逐项测试：

1. 优先保持 `CONFIG_MBEDTLS_INTERNAL_MEM_ALLOC=y`，打开：
   - `CONFIG_MBEDTLS_DYNAMIC_BUFFER=y`
   - `CONFIG_MBEDTLS_DYNAMIC_FREE_CONFIG_DATA=y`
   - `CONFIG_MBEDTLS_DYNAMIC_FREE_CA_CERT=y`
2. 若 SDK 不需要握手后访问 peer certificate，单独测试关闭 `CONFIG_MBEDTLS_SSL_KEEP_PEER_CERTIFICATE`；官方示例约节省 3.7 KB。
3. 只有在以上仍不足时，才测试 `CONFIG_MBEDTLS_DEFAULT_MEM_ALLOC` 或 `CONFIG_MBEDTLS_EXTERNAL_MEM_ALLOC`。当前 Flash Encryption 关闭，生产版本将 TLS token/证书/会话对象迁入 PSRAM 前必须完成安全评审。ESP-IDF 说明 flash encryption 开启时才会自动加密外部 RAM 数据。
4. 不建议先降低 `CONFIG_MBEDTLS_SSL_IN_CONTENT_LEN=16384`。除非确认所有服务端 TLS record 或 Maximum Fragment Length 协商满足更小上限，否则可能直接导致握手失败或 `MBEDTLS_ERR_SSL_INVALID_RECORD`。
5. 不把关闭硬件 AES 作为正式方案；它只适合作为诊断 A/B，代价是 CPU 占用和实时媒体余量下降。

## 不建议采用的方案

- **换 16 MB PSRAM 模组来解决当前问题**：当前 8 MB PSRAM 尚未接近耗尽，而内部 SRAM/DMA capability 不会随 PSRAM 容量增加。N16R16VA 还是不同的 1.8 V PSRAM 模组，不能仅按容量直接替换。
- **把所有任务栈放入 PSRAM**：PSRAM 在 flash cache 被禁用时不可访问；`xTaskCreate()` 默认也不会这样做。网络、NVS、OTA、TLS 和实时媒体任务不适合盲目外移栈。
- **只降低 `CONFIG_SPIRAM_MALLOC_ALWAYSINTERNAL`**：全局阈值会影响第三方库和延迟敏感小对象，收益不可控。先用 capability allocator 显式迁移已审计的大对象。
- **把 I2S/Wi-Fi/AES DMA 描述符放入 PSRAM**：ESP-IDF 明确要求 DMA transaction descriptors 保留内部内存。
- **同时修改 camera DMA、Wi-Fi、TLS 和任务栈**：一旦出现画面损坏、弱网掉帧或握手异常，将无法归因。

## 预期收益

以下收益不能简单全部相加，但足以说明无需升级 PSRAM：

| 改动 | 内部 SRAM 预期改善 |
|---|---:|
| camera PSRAM DMA | 约 30.7 KB |
| TiRTC 握手前延迟 platform worker/queue | 约 33.4 KB 启动峰值 |
| 平台请求 PSRAM pool | 约 8.8 KB 常驻 + 降低栈峰值 |
| 音频 PSRAM pool | 约 12.2 KB 常驻 + 降低栈峰值 |
| 网络 external BSS | 约 14.3 KB |
| Wi-Fi static RX 10 → 4 | 9.6 KB |
| mbedTLS dynamic buffers | 官方示例约 20 KB，需 TiRTC HIL |

仅完成 camera DMA、队列池、worker 生命周期和网络 BSS，TLS 启动阶段理论上就能增加约 60–90 KB 内部余量，且 PSRAM 新增长期占用远小于 1 MB。

## HIL 验收门槛

每个阶段生成独立 BIN SHA-256，并绑定测试记录：

1. 冷启动 20 次，TiRTC 启动成功率 100%，无 alloc failure、stack overflow、watchdog。
2. AI start/stop 100 次；H5/AI 抢占切换 100 次。
3. H5 MJPEG 8 fps + 对讲连续 2 小时，无花屏、音频爆音、持续掉帧或内存下降趋势。
4. Wi-Fi 断开/恢复 20 次，MQTT/TiRTC 能收敛恢复。
5. 记录每阶段 internal/DMA/PSRAM 的 free、minimum、largest；运行两小时后 largest block 不应持续单向下降。
6. `.33` 启动 HIL 基线：TiRTC pre-init 时 `DMA largest = 32 KB`、`DMA free ≈ 39.5 KB`，已成功进入 `SDK started`；压力测试要求这些数值不持续单向下降。这是本项目实测基线，不是 Espressif 的通用保证值。
7. 所有应用任务在最重路径后保留实测安全余量，不再仅依靠静态调用链估算。

## 推荐实施顺序

1. 阶段 0：观测和失败分配 hook。
2. 阶段 1：camera PSRAM DMA，单独 HIL。
3. 阶段 2：延迟/复用 platform worker，单独 HIL。
4. 阶段 3：平台和音频固定 PSRAM pool。
5. 阶段 4：HTTP/JPEG PSRAM 工作区，并根据高水位收缩任务栈。
6. 阶段 5：network external BSS 和 Wi-Fi static RX 调整。
7. 阶段 6：仅在仍有必要时调整 mbedTLS。

这一路径优先使用 ESP32-S3 已有的 PSRAM/DMA 能力，同时保留内部 SRAM 给真正必须依赖它的 TLS、任务栈和 DMA 控制结构，风险明显低于全局搬迁或直接更换模组。
