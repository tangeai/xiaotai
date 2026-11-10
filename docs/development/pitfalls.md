# 二次开发易错点与回归入口

[中文](pitfalls.md) · [English](pitfalls.en.md)

修改界面、音频、会话或打包流程前，先核对本页。这里保留长期有效的约束与回归入口，
构建身份、原始症状和联调过程见[历史回归记录](../../tests/DEVELOPMENT_REGRESSION_NOTES.md)。
主机测试不替代板级触摸、DMA（直接存储器访问）和实际听音验收。

## 经验放在哪里

开发与发布要求以[贡献指南](../../CONTRIBUTING.md)和[架构](../../ARCHITECTURE.md)为准；
测试方法见[测试说明](../../tests/README.md)。新增经验优先补充已有条目并关联测试，
不另建重复规则。以下命令均从仓库根目录执行；完整检查使用 `bash tools/check.sh`。

## BK 待机重启：原子操作还受内存区域约束

C11 原子操作在主机上正确，不代表它在目标芯片的任意 SRAM 区域均可安全使用。
BK 的 `platforms/beken/runtime/src/xiaotai_tirtc.c` 将关闭交接原子状态放在固定 SDK 的
`.sram_spinlock_section`，遵守独占监视器的内存区域限制。保留目标编译器的段归属检查。

心跳超时是保护动作，最后一条 HTTP、音频或 RTC 日志不能单独定位根因；
不能靠延长超时掩盖调度停顿。芯片内存行为和长时间媒体并发仍需实板验证。

回归：`test_bk_room_close_callback.py`、`test_bk_event_reentry.py`、
`test_bk_close_reconnect.py`，均位于 `tools/tests/`。

## 异步房间操作必须能超时、识别旧回复并承受重复通知

`product/src/xiaotai_room.c` 在提交请求前保存独立请求编号、连接代次与截止时间，
适配器回传原请求身份。分配或队列失败不能导致永久等待，迟到回复不能完成新请求。
连接成功不等于加入成功；连接、加入和租约需分别处理超时。

平台关闭通知须同时核对房间 ID 和分配版本。重复成功回复不能重复启动媒体，
旧分配不能复活已关闭房间。恢复流程不增加后台分配轮询，刷新入口仍遵守
[交互规范](../product/PRODUCT_INTERACTION_PROFILES.md#64-房间分配刷新)。

回归：`test_room_async_recovery.py`、`test_esp_signal_recovery.py`、
`test_product_core.py`，均位于 `tools/tests/`。还需覆盖真实弱网与 SDK 调度。

## ESP MQTT 上线须等必需订阅得到确认

`platform_client.c` 必须收到命令和通知两个订阅的成功 SUBACK（订阅确认）后才报告在线。
订阅提交失败或被拒绝时断开并重连；断线清除本轮状态，重复或不匹配确认不能提前上线。
仅建立 MQTT 连接不能证明设备能接收平台控制消息。

回归：`python3 tools/tests/test_esp_signal_recovery.py`。
实板还需验证真实消息服务器拒绝订阅后的恢复。

## 固件打包须核对二进制身份，验证状态须贯通命令行

`tools/esp_image_identity.py` 核对目标芯片、版本、镜像边界、校验和与 ELF 摘要，
`tools/package_esp_validation.py` 在写包前执行检查。统一入口与独立入口须传递相同验证状态。

镜像匹配不等于源码提交匹配。当前 ESP 打包的 `source_commit` 留空，打包时 HEAD
单列为 `packaging_source_commit`；不能把当前检出的提交当成旧镜像的来源。
构建成功也不能证明 HIL（硬件在环测试）通过。交付列出原始路径、版本和 SHA-256。

回归：`python3 tools/tests/test_esp_package_identity.py`、
`python3 tools/tests/test_validation_packaging.py`。

## 房间数字键不能与普通按钮共用操作编号

ESP 界面适配器 `starter_product.c` 的普通动作、数字键和联系人使用独立编号范围。
新增动作须保留 `_Static_assert` 检查。固定数字触发固定错误操作时，先查编号冲突。

测试应捕获实际渲染按钮的编号，再交给生产分发代码，不能在测试里另写一套编号。
回归：`python3 tools/tests/test_room_keypad.py`。更换布局后仍需检查触摸命中区域。

## 重新加入房间必须初始化本次租约

ESP 加入回调接受本轮成功回复时，应在提交在线状态请求前初始化租约与续期时间。
不能等待异步 HTTP 返回，否则旧租约可能提前关闭新连接。退出时清理租约，重复回复
不能延长租约，旧代次及无效回复不能启动租约。

回归：`python3 tools/tests/test_room_lease_lifecycle.py`。
还需实板验证连续创建、退出及重新加入。

## 顶层提示条必须按其父层居中

普通页面偏移不能直接用于 `lv_layer_top()` 上的提示条。文字居中与提示条居中分别设置，
提示条使用 `LV_ALIGN_TOP_MID`，纵向距离按屏幕高度缩放。

回归：`python3 tools/tests/test_tip_layout.py`。
实机检查不同布局下的遮挡、旋转与触摸区域。

## AI 音频须容纳突发下发，并排空尾音后退出

网络到包节奏不等于扬声器播放节奏。AI 队列须容纳突发音频，不能通过加快播放清理积压；
有限队列也不能保证所有弱网或超容量场景无丢包。

服务端结束或远端关闭后，按退出策略排空已接收音频。退出判断须覆盖接收队列、
解码或写入中的帧和板级播放尾部；队列空、写入成功均不等于扬声器已播完。
用户主动取消、排空超时及陈旧会话清理遵守各自策略，不能一概等待或一概丢弃。

排查时同时记录接收、解码、写入、溢出、待播放与播放中帧数、PCM 时长及播放尾部。
累计音频时长与整个对话耗时的差值包含对话空白，不能直接用来判断播放速度。
设备不能补播服务端未发送的音频。

回归：`test_p4_ai_playback.py`、`test_bk_ai_playback.py`、
`test_bk_disconnect_playback.py`，均位于 `tools/tests/`。
还需逐板听音，验证正常结束、远端关闭、主动取消及连续重试。

## 断开原因与其他告警必须保留证据边界

心跳发送不等于超时，通用传输返回码不能单独证明“服务端空闲超时”。
正常结束的关闭回调也不能证明尾音丢失，须结合结束命令与最终排空统计判断。

BK 的 `Task exits abnormally!` 可能来自 SDK 的任务函数返回路径，本身不能证明死锁。
保留 SDK 断开调用、回调入口和工作线程处理完成的边界日志，再定位阻塞阶段。
临时日志可能改变时序，不能把偶发成功当作修复；铃声、唤醒缓存和通话播放须分别验证。

## BK 多人对讲松手必须可靠送达

触摸释放不能因控制队列满而丢失；释放代次独立保存，由控制任务发出停止意图。
按住期间读取真实触摸状态，释放后恢复中断等待，不能以队列为空或按住时长判断松手。
入口为 `touch_event`、`service_room_touch_release` 和 SDK 的 `tp_process_task`。

回归：`test_bk_ptt_release_delivery.py`、`test_bk_touch_release_poll.py`，位于 `tools/tests/`。
实板验证长按、快速连按、移出按钮松手及平台通知并发时均能停止上行。

## ESP32：平台解绑不能在 PSRAM 任务栈直接擦除 NVS

Flash 擦写可能关闭缓存，PSRAM 任务栈不能直接执行这段操作；增大栈或屏蔽断言不能解决。
`runtime_config_clear_tirtc()` 为此使用内部 RAM 栈任务，并等待擦除与提交完成后返回。
创建任务、擦除或提交失败均须返回错误；只清除绑定凭证，保留网络与产品设置。

回归：`python3 tools/tests/test_unbind_flash_stack.py`。
还需在空闲、AI 和通话期间实板解绑，检查重启后的绑定页、网络保留及重新绑定。

## 绑定 ACK 已到达，仍等待三次验证码语音

收到匹配的绑定 ACK（确认消息）后，应取消验证码播报；每次播放前后和间隔中均检查取消。
只在凭证保存成功后提示绑定完成，受控重启前给用户留出可见提示时间。
异步取消不得阻塞消息处理，旧播放代次也不能取消新一轮播报。

回归：`python3 tools/tests/test_binding_completion.py`。
实板检查播报中途绑定、成功提示、重新绑定及再次上线。

## ESP32-S3：写入计数不能代替播放排空

共享运行时需要 S3 填充队列、工作线程持有帧与 DMA 尾部状态。帧发布入队前增加计数，
入队失败、消费和清队列各自归还所有权；换代不能直接清零。64 位累计量和尾部时间需同步保护。
板级 DMA 容量估算不是真实播放水位，仍需录音或实际听音核对。

回归：`test_s3_playback_drain.py`、`test_s3_opus_media.py`，位于 `tools/tests/`。

## P4/BK：多个原子字段不等于一致的排空快照

分别读取多个原子字段，仍可能拼出不存在的“队列空、尾部已过期”状态。
写线程先发布尾部期限，再释放帧所有权；读线程先读取待播放及播放中状态，再读尾部期限，
保留 acquire/release 内存顺序。不要依赖 C 函数实参的求值顺序。

回归沿用 P4/BK AI 播放及断开测试，须驱动实际生产状态入口；主机通过后仍需逐板听音。

## ESP32-S3/P4：唤醒回应与异步连接必须分别控制音频准入

网络请求可与本地回应异步进行，媒体启动仍须同时等待平台接受和本地输入就绪。
回应期间关闭缓存准入，结束后保留尾音隔离，只接收边界之后的完整采集帧；
不能只靠 AEC 或一次清空队列阻止迟到帧进入。

准入状态绑定本次 token（会话请求标识）。取消、静音、播放拒绝与过期队列项均须完成清理；
旧 token 的完成不能放开新会话的麦克风，回应超时不能直接放行。
入口为 `starter_media_prepare_ai_preroll`、`call_ring_task` 和 `service_ai_media_admission`。

回归：`tools/tests/test_wake_ack_overlap.py`、`tools/tests/test_wake_audio_boundary.py`，
以及 S3 原生工程中的 `tools/test_voice_reliability.py`。
实板验证快慢网络、最大音量与混响、回应后首字以及连续取消重试。

## 后续维护

新增条目说明症状、已确认原因或待验证假设、实现约束、生产入口及回归方式。
修复先重放失败场景，再修改代码，并将回归登记到 `tools/check.sh`。
历史取证单独归档，不把单块板、单个构建的结论推广到全部平台与版本。
