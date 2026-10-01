# ATK-DNESP32S3 内存预算与实测项

目标硬件按厂家资料为 16 MB Flash / 8 MB PSRAM；实际板卡容量必须由启动日志确认。当前配置为 2 个 PSRAM 摄像头帧缓冲、QVGA JPEG、16 kHz 双向 I²S，TiRTC 最大发送缓冲 256 KiB。媒体层拒绝大于 128 KiB 的 JPEG，并在发送缓冲达到 192 KiB 时暂停视频；该限制约束应用提交的帧，不代表摄像头驱动实际分配的帧缓冲上限。

| 已由源代码限制的项目 | 上限或配置 | 内存位置/待验证 |
| --- | ---: | --- |
| TiRTC 发送缓冲 | 256 KiB | SDK 分配区域须真机测量 |
| 视频提交单帧 | 128 KiB | 摄像头驱动帧缓冲在 PSRAM |
| 摄像头帧缓冲 | 2 个 | 实际大小由 `esp32-camera` 运行时决定 |
| 下行 A-law 队列 | 8 × (1500 B + 帧元数据) | FreeRTOS 队列堆，需测量 |
| 绑定语音 PCM | 约 378 KiB | 固件 Flash 映射，不复制到堆 |
| 应用任务栈 | 启动/HTTP 各 24 KiB；运行时 12 KiB；媒体任务合计约 18 KiB | ESP-IDF 分配，需测量内部堆压力 |
| 内部堆保留配置 | 128 KiB | `CONFIG_SPIRAM_MALLOC_RESERVE_INTERNAL` |

这些是可审计的静态边界，尚不能证明 Wi-Fi、TLS、TiRTC 和相机同时启动后的内存余量，因此 Hardware IR 的 `startup_and_media_budgeted` 保持未知。固件启动日志新增媒体初始化后的内部堆空闲/最大连续块与 PSRAM 空闲；串口 `status` 也可再次读取。第一次实物验证应记录联网前后、H5 订阅前后、双向语音期间的这些值，以及摄像头 PID、发送缓冲和掉帧。
