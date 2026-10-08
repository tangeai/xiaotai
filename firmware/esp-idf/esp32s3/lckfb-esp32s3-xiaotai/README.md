# lckfb_esp32s3_xiaotai

这是立创·实战派 ESP32-S3 开发板 PCB V1.0.1 的 AIROBOT 语音版固件，
板上模组为 `ESP32-S3-WROOM-1-N16R8`。固件使用 ESP-IDF 5.5.x、TiRTC SDK
2.3.0 和 LVGL 8.3.11。工程包含 320×240 触控产品界面、设备配网/绑定上线、
AI 双向语音、设备/微信纯语音呼叫，以及原有 H5 MJPEG 实时画面与双向语音能力。
S3 产品界面不显示视频通话、本地预览或摄像头开关。

板级媒体路径已经接入资料记载的 GC0308、实机精确探测到的 GC2145，以及 ES7210、ES8311、PCA9557 和 NS4150B。摄像头以 QVGA RGB565 采集，再由 `esp32-camera` 软件转换为完整 JPEG；摄像头门禁只接受 GC0308 PID 或 GC2145 PID `0x2145`，其他传感器仍拒绝。AI 传输音频为 Opus、16 kHz、单声道、20 ms，目标码率 16 kbit/s；H5、设备呼叫、微信通话和多人对讲为 G.711 A-law、8 kHz、单声道。硬件采集和播放为 16 kHz。AI 下行保留最多 64 个压缩包，按 20 ms 帧计可容纳 1.28 秒；首次播放仍只预缓冲 4 包且最多等待 80 ms，不通过改变采样率加速消耗积压。突发数据可能形成较长待播积压，停止或切换连接时必须清空旧队列；超过容量仍会明确计入 overflow。ES7210 RX 与 ES8311 TX 一次性注册为 I2S0 配对通道，标准 I2S 的两个 32-bit slot 与 TDM 的四个 16-bit slot 都使用 64 BCLK/frame，因此可在共享 MCLK/BCLK/WS 上同时采集和播放。Espressif ESP-SR 2.4.7 的 `AEC_MODE_FD_LOW_COST` 同时处理 MIC1 近端语音和 MIC3 硬件回采参考；AI 直接将 16 kHz AEC 输出组成 20 ms Opus 包；其他远程链路将 AEC 输出降采样为 20 ms/8 kHz G.711 包。本地唤醒使用 Voicute TFLite 和“你好小钛”模型，独立 CPU0 任务读取 AEC clean 最新窗口；摄像头事件、软件 JPEG 和浮点 AEC 任务固定在 CPU1，AEC/JPEG 循环主动让出 RTOS tick。

## 构建和烧录

准备 ESP-IDF 5.5.x 环境后执行：

```bash
idf.py set-target esp32s3
idf.py build
# 确认目标串口后执行：
idf.py -p <SERIAL_PORT> flash monitor
```

工程目录迁移后若提示 `build` 属于另一个 project，说明其中保留了旧机器的 CMake 绝对路径。不要复制 `build/`；移走该目录后在新路径重新执行 `idf.py build`。

每次链接 ELF 后，CMake 会自动运行 I2C、绑定、UI、NTP、启动顺序、本地语音、摄像头、媒体实时性、I2S/AEC、内存、JPEG 和 AI/TLS 栈等门禁。绑定门禁要求临时 MQTT 先完成订阅，再用同一次 Report 的 `temp_token` 下载服务端验证码 PCM；验证码必须显示在专用页面、经板级媒体路径播报三次，且禁止写入串口日志。语音门禁检查独立推理、有界最新窗口和过期结果拒绝，最终 ELF 不含 MultiNet 或 ESP-SR 模型加载器。TFLite 模型内嵌于应用，无需额外模型分区。ESP-SR 仅因 AEC 依赖保留。AEC 门禁锁定 MIC1 槽 0、MIC3 参考槽 1、`FD_LOW_COST`、aggressive NLP、对齐 PSRAM 工作帧以及 AI 20 ms Opus / 其他会话 20 ms G.711 输出。I2S 门禁要求 TX/RX 在同一次 `i2s_new_channel()` 中注册，并禁止重新引入 I2S1 master 或播放时暂停采集。当前板级媒体链统一使用 driver_ng I2C；如果最终镜像同时出现 legacy `i2c_driver_install` 和 driver_ng `i2c_new_master_bus`，构建会直接失败。direct PSRAM camera DMA 虽能节省约 30 KiB 内部 SRAM，但实机截图已证明它会破坏本板 QVGA RGB565 行数据，因此禁止重新启用。AI/TLS 门禁从最终 ELF 读取实际 Xtensa 栈帧并保留 SDK/RTOS 安全余量。不要绕过这些门禁。

当前固件包含“你好小钛”专用唤醒模型、AI 预录交接以及 H5/AI/呼叫切换的迟到事件防护。不引入 MultiNet 或本地命令识别，BOOT 单击由运行时按当前状态启动/结束 AI、接听/挂断通话或结束本机房间媒体；双击拒接来电或呼叫平台列表第一个联系人。屏幕保留 AI 入口。`voice-diag 20` 查看概率、推理耗时与音频强度；`voice-config 700` 临时设置阈值（重启恢复构建配置，默认 700）。声学结论必须来自当前固件的实机验收，构建通过不代表准确率通过。

`idf.py menuconfig` → `XiaoTai application` 可配置发现地址、门户和默认唤醒阈值。仓库主机回归入口为 `bash ../../../../tools/run_host_tests.sh`；项目内入口仅保留兼容转发。项目发布条件与二次开发约束见根目录 [CONTRIBUTING.md](../../../../CONTRIBUTING.md) 和 [THIRD_PARTY.md](../../../../THIRD_PARTY.md)。

“三个点 → 运行状态”包含资源、音频和最近事件三页，查看时不结束 AI。进入通讯录、表情包、设置、网络等普通功能页会请求结束 AI；单纯打开菜单、返回和进入 AI 聊天不会结束 AI。人际呼入／呼出统一抢占 AI 与 H5 实时查看，人际通话之间按忙线处理。交互合同见[产品交互规范](../../../../docs/product/PRODUCT_INTERACTION_PROFILES.md)。

日常构建使用默认 `build/`，直接执行 `idf.py build`、`idf.py flash monitor`，或已有的 `flash` 快捷命令。不要复制其他路径生成的 `build/`。构建身份和验证要求见[测试与硬件验证](../../../../tests/README.md)。



H5 目标为 8 fps；JPEG 转换的 jpge/to_jpg/yuv 源文件单独使用 `-O2`，其他模块保留所选调试配置。下一帧等待最多 20 ms，临近 125 ms 帧期限时缩短等待，避免调度额外降低帧率。实际 8 fps 仍须以本次固件的实机编码耗时及连续发送计数确认。此优化不增加并发帧数：摄像头仍使用两个 153,600-byte QVGA RGB565 PSRAM framebuffer，JPEG 转换与发送仍串行处理一帧。摄像头 DMA 先写入约 30 KiB 内部中转缓冲，再复制到 PSRAM framebuffer；64 个下行音频 slot（AI 可用 64 个，其他实时会话准入上限仍为 24 个）、4 个平台请求 slot、8 KiB HTTP scratch、128 KiB JPEG 输出缓冲、播放升采样缓冲和 AEC 计算帧均显式放入 PSRAM，FreeRTOS 队列只传 1-byte 索引。AEC 的 I2S 目标缓冲保留在内部 SRAM，算法状态通过 `MALLOC_CAP_SPIRAM` 分配。历史 AEC 基线 ELF 静态 DIRAM 为 144,231 bytes（42.2%），不是当前 Voicute 固件的资源测量；启动日志会进一步输出 AEC 初始化实际消耗的 internal/PSRAM 字节数，以及全局 internal/DMA/PSRAM 的 free、minimum 和 largest。`status` 还会输出 AEC/JPEG 最大处理耗时与 deadline-miss。分配失败时会记录申请大小、capability 和函数名。

工程默认使用 16 MB Flash、8 MB Octal PSRAM 和 USB Serial/JTAG 控制台。其他硬件规格需要同步调整 `sdkconfig.defaults`、`partitions.csv` 和板级驱动。

## 首次启动

1. 设备没有 Wi-Fi 配置或保存的网络持续连接失败时，屏幕进入“需要连接网络”页并启动名称为 `XiaoTai-XXXX` 的开放 SoftAP，无需密码。手机连接后通过通配 DNS 和 HTTP 探测重定向引导系统打开 `http://192.168.6.1`；自动弹窗受客户端系统策略影响，未弹出时直接手动访问该地址。
2. 设备联网但尚未绑定时，屏幕进入“绑定设备”页并用 48 px 大号数字显示 6 位验证码。临时 MQTT 完成订阅后，设备使用同一次上报返回的临时令牌下载 8 kHz PCM，通过板载扬声器连续播报三次；验证码不写 NVS，也不打印到串口。
3. 在 H5 输入屏幕上的验证码完成设备绑定。凭证保存到 NVS，设备随后完成服务发现、MQTT 登录和 TiRTC 启动。
4. 开机先核对 `firmware version=... elf-sha256=...`；也可以在串口输入 `version` 单独查看固件身份，或输入 `status` 同时查看固件身份、平台、MQTT、TiRTC、会话和媒体计数。

普通 `idf.py flash` 会保留 NVS。后续启动如果已有有效设备凭证，会打印
`stored device binding found` 并跳过验证码；串口执行 `tirtc-clear` 只清除设备绑定
凭证并自动重启，不会清除 Wi-Fi 配置。

当前联调 profile 通过 `http://ep-open.tangeopen.com/services` 完成平台服务发现。
工程仍保留 TLS 客户端、TLS 1.2、证书包和启动前时间同步。按当前产品选择，工程
不设置 `TIRTC_OPT_SERVICE_ENDPOINT`，TiRTC 始终使用 SDK 内置入口；平台发现使用
HTTP 不代表 TiRTC SDK 链路也使用 HTTP。

## AIROBOT S3 产品功能

- 首页提供程序化表情和日期/时钟两个表盘，支持左右滑动或点击页码切换；主区域单击进入 AI 对话，右下角保留 40×40 菜单触点。
- AI 对话页显示聆听、思考和回复状态，当前字幕固定使用“你：”或“AI：”前缀；最终字幕组成当前会话历史。打开菜单本身不结束 AI，进入普通功能页才请求结束会话。
- 麦克风 20 ms 音频帧持续计算活动强度，经平滑和 300 ms 档位保持后驱动表情幅度；待机环境音只唤醒背光和本地显示，不会建立云端会话。
- 待机说“你好小钛”投递现有 AI 唤醒请求。模型就绪后等待 4 秒启动安静期，再收集完整窗口；AI、H5 媒体、呼叫或闭麦期间暂停识别，恢复时重新收集音频。按键、屏幕和呼叫优先级保留。
- 全局麦克风静音清空预录缓存并阻止旧上行帧跨静音周期发送。用 `status` 查看音频、AEC 和会话状态；本地语音应显示 `wake=voicute-tflite`，模型成功自检后 `ready=1`。
- 通讯录从服务发现得到的 call/voip 服务同步。列表先进入联系人详情，再由详情发起纯语音呼叫；微信联系人不伪造在线状态，离线设备联系人禁用呼叫。
- 设备呼叫固定请求 `call_type=audio` 并使用 P2P；微信呼叫固定 `wx_room_type=voice` 并使用 WHIP。来电优先级高于 H5/AI，媒体在业务 `0x2000` 确认前保持关闭。
- 来电页提供接听/拒绝，通话页提供全局麦克风静音和固定底部红色挂断按钮；拒接、无人接听、对方挂断和网络中断会显示结果页。视频来电会被明确拒绝。
- 设置支持 0–10 级扬声器音量、扬声器/麦克风静音，以及 1/5/10/30 分钟/从不息屏；默认 5 分钟并持久化到 NVS。息屏真正关闭 GPIO42 背光 PWM，触摸、声音和来电均可唤醒。
- 服务地址由启动服务发现解析，产品任务不执行阻塞网络请求；LVGL、音频、平台和 TiRTC 保持分任务运行。

也可以使用以下联调命令预置或清理配置：

```text
version
wifi-set <ssid> <password>
wifi-clear
tirtc-set <device_id> <device_secret> [client_id]
tirtc-clear
restart
```

`tirtc-set` 只用于受控联调环境。不要在日志、脚本或版本库中保存真实设备密钥。

## H5 画面与对讲

设备处于 `waiting` 时接受一个 H5 连接：

- 音频上行使用 stream `10`，格式为 G.711 A-law、8 kHz、单声道；
- 视频上行使用 stream `11`，格式为 MJPEG；每次 SDK 发送调用承载一张完整 JPEG，媒体类型为 `TIRTC_VIDEO_JPEG`；
- 常规发送目标为 8 fps（125 ms/帧）；H5 刷新请求可触发下一张完整 JPEG 提前发送；
- H5 下行语音使用 stream `14`，SDK 回调复制到固定队列后由媒体任务消费；
- 下行 8 kHz PCM 线性升采样到 16 kHz；ES7210 MIC3 同步回采实际 DAC 路径，和 MIC1 一起进入 ESP-SR AEC；
- H5 请求刷新帧时，`starter_media_request_key_frame()` 让摄像头任务尽快发送下一张完整 JPEG。

下行音频会在 SDK 回调返回前复制到固定队列，再由独立任务解码并写入 ES8311。队列满、会话代次过期或编码不符的帧都会被丢弃。

## AI 对讲

板载 `B/BOOT` 键连接 GPIO0，按下为低电平。设备在线后短按一次启动 AI
对讲，再按一次（包括 `ai-connecting` 阶段）结束 AI 对讲。按键采用 50 ms
软件消抖，按住只触发一次；如果上电时已经按住，必须先松开才会响应。不要在
按住 `B/BOOT` 时按 `R/RESET` 或重新上电，否则 ESP32-S3 会进入下载模式。

`R/RESET` 直接连接芯片复位线，按下会重启，不是软件可编程按键。官方
V1.0.1 原理图没有第三个 `P` 电源按键，Type-C 插电即上电；软件“关机/唤醒”
需要另行设计深度睡眠和唤醒源，也不等同于切断硬件电源。

串口仍可执行：

```text
ai-start
ai-stop
```

`ai-start` 依次完成 AI token 请求、WHIP 建连和 `start_session`。只有服务端确认 `start_session` 后，运行时才允许板级媒体适配器发送音频。AI 返回的 stream `1` 音频进入与 H5 对讲相同的下行队列和 AEC 参考路径。`ai-stop` 发送 `end_session`、停止媒体并重新等待 H5 连接。AEC 是常驻算法实例，声学路径不变时可保留自适应状态；会话代次变化时，未凑满的 20 ms 上行包会被丢弃，避免跨会话混音。

TiRTC 2.3.0 的 `TiRtcWhipConnect` 在当前 ELF 中自身需要约 8 KiB 栈。AI token 和 WHIP URL 使用短生命周期堆对象，避免再与 SDK 建连栈帧叠加；会话任务使用 24 KiB PSRAM 栈。`.57` 真机普通 AI 会话中，原 13 KiB 栈的历史最低余量只有 428 bytes，因此 `.58` 增加栈预算；当前 ELF 已知同步链为 8,416 bytes，构建门禁要求保留 SDK/RTOS 嵌套余量并禁止继续无审计扩张。外部 AI/VoIP 连接前会暂停并销毁 MQTT 客户端，连接回调后恢复，释放 TLS 所需连续内部 SRAM。`status` 的 `stack-min-free` 是该任务启动以来的最小剩余栈字节数；AI 建连后应保存该值作为实机验收证据。

## 代码边界

```text
main/                         组合根、首次绑定和启动顺序
components/starter_runtime/   H5/AI/设备呼叫/微信呼叫单一会话状态任务
components/starter_tirtc/     TiRTC SDK 类型和回调适配
components/starter_media/     V1.0.1 摄像头、音频 codec、ESP-SR AEC、功放与媒体任务
components/starter_product/   ST7789/FT6336/LVGL、页面路由、背光与产品设置
components/starter_button/    V1.0.1 BOOT/用户按键消抖与 AI 启停意图
components/starter_console/   最小串口命令
components/platform_client/   服务发现、设备 HTTP/MQTT
components/wifi_manager/      Wi-Fi 与 SoftAP 配置
components/runtime_config/    NVS 设备凭证
third_party/tirtc/             TiRTC SDK 2.3.0
```

会话状态只在 `starter_runtime` 任务中改变。TiRTC 回调只投递有长度上限的事件或把音频复制到固定队列；回调中不执行 HTTP、阻塞等待、SDK Stop 或 Uninit。

## 建议阅读顺序

代码中的模块头部和公开头文件说明了职责、调用顺序、线程归属和数据生命周期。首次接入建议按以下顺序阅读：

1. `../../../../platforms/esp-idf/main/app_main.c`：查看启动、绑定和模块组装顺序；
2. `../../../../platforms/esp-idf/components/starter_media_common/include/starter_media.h`：了解跨开发板媒体接入契约；
3. `../../../../platforms/esp-idf/components/starter_media_common/src/starter_aec.c`：查看归一化双通道、ESP-SR AEC 和 16→8 kHz 降采样；
4. `components/starter_media/src/starter_media.c`：查看采集、JPEG、G.711、播放升采样和 I2S0 配对全双工；
5. `../../../../platforms/esp-idf/components/starter_runtime/include/starter_runtime.h`：了解产品控制入口和公开状态；
6. `../../../../platforms/esp-idf/components/starter_runtime/src/starter_runtime.c`：需要排查会话问题时再阅读状态机；
7. `components/starter_tirtc/`：只在核对 SDK stream、回调或连接生命周期时阅读。

`platform_client`、`wifi_manager` 和 `runtime_config` 隐藏平台接入细节。普通板级音视频移植不需要修改这些模块。

## 尚未完成的产品化工作

代码中的 `TODO(product-...)` 是预留的后续适配点：

- `TODO(product-media-display)`：若未来在 P4 产品上显示 H5 下行视频，增加非阻塞视频接收、解码和显示适配；S3 产品不使用该入口；
- `TODO(product-security)`：使用加密 NVS 或安全芯片保护设备凭证。

可以随时执行以下命令检查未完成的产品适配点：

```bash
rg -n 'TODO\(product-' main components
```

实机 GC2145 已确认能够输出 H5 MJPEG，Web → 设备和设备 → Web 两个音频方向也已分别听证。同一 SSID 下 `-75 dBm` 的 AP 明显卡顿，`-28 dBm` 的另一 BSSID 明显改善；因此后续必须同时保存 BSSID/RSSI 和媒体队列指标，不能只调大缓冲。当前 AEC 版本仍需在同一会话中验证 `audio-tx`、`audio-rx` 和 `AEC processed` 持续增长，并量测远端单讲回声衰减、近端/远端双讲可懂度和 AI barge-in。`status` 的 `clipped-mic/ref` 应保持为 0；若参考削顶，应先降低 ES8311/ES7210 增益而不是加强 NLP。AEC 已进入构建策略，但尚未取得精确固件的 HIL 声学证据。厂商资料写 GC0308、实物探测为 GC2145 的模组/BOM 差异已经记录到 Hardware IR，不能静默视为同一型号。

## 验收顺序

1. `idf.py build` 成功并通过全部策略；启动日志不再出现 I2C 冲突、I2S 控制器占用/GPIO 不可用或 `expected GC0308`，而应出现 `accepted camera sensor GC2145, PID=0x2145`、`ESP-SR 2.4.7 AEC ready` （不再出现 MultiNet 就绪日志），随后显示 TiRTC SDK 版本、build info，以及各启动边界的 internal/DMA/PSRAM 余量。
2. 首次配网和验证码绑定成功，`status` 显示 platform、MQTT、TiRTC 就绪。
3. H5 连接后持续显示 MJPEG，音频/视频发送计数增长，下行语音接收计数增长。
4. H5 按住说话或 AI 回复播放期间，设备上行收音持续运行，`audio-tx`、`audio-rx` 与 `AEC processed` 同时增长，`AEC errors=0`、`clipped-mic/ref=0/0`。
5. 只播放远端语音且设备旁无人说话时，Web/AI 端不应清楚听回自己的声音；同时说话时，近端语音不能被 NLP 完全吞掉。
6. 短按 `B/BOOT`（或执行 `ai-start`）后状态从 `ai-connecting` 进入 `ai-active`；AI 回复期间近端插话应可被识别，且回复不应反复触发自身输入。
7. 再短按一次 `B/BOOT`（或执行 `ai-stop`）后状态回到 `waiting`，H5 可以重新连接。

这是已通过编译的板级移植工程，不是量产固件。产品还需要完成弱网策略、看门狗、凭证保护、长期稳定性和实机音视频验收。
