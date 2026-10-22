# 小钛测试与硬件验证

小钛测试分成四层。低层测试通过不代表产品可用；发布前必须逐层通过，并保留板卡
型号、固件提交号和串口日志。

## 一键主机测试

在仓库根目录执行完整检查：

```bash
bash tools/check.sh
```

`tools/check.sh` 会执行全部主机测试、板卡 manifest 校验和补丁空白检查。若只需要
重复运行主机测试，可执行 `bash tools/run_host_tests.sh`。

`test_onboarding_faults.py` 还回放带旧 `CONFIG_XIAOTAI_PORTAL_URL` 的真实
ESP 绑定入口，捕获串口输出并与屏幕文案核对。旧实现输出演示站而失败；修正后
共同使用产品层 `https://xiaotai.chat`。该配置只用于提示，移除不影响服务发现、
API 或 MQTT 地址；原有绑定显示、存储失败和重试测试继续覆盖流程。

产品核心测试还会生成 `tests/.local/reports/product-core-coverage.json`，并要求生产
代码行覆盖率不低于 88%，分支覆盖率不低于 57%。会话运行时单独要求 95% 行、65%
分支，呼叫状态要求 95% 行、60% 分支，媒体参数契约要求 100%，多人对讲要求
80% 行、50% 分支。这些数值是防止
覆盖率倒退的最低线，不代表达到数字就可以省略业务验收或实板测试。当前覆盖较低的
模块应优先补正常、失败、超时、重复事件和陈旧事件用例，再逐步提高门槛。

该入口会执行：

- 产品核心单元测试：会话状态、AI 协议、呼叫协议、联系人、信令和视频节拍；
- 产品业务场景：H5 实时查看到微信来电的交接、AI 呼叫意图、媒体所有权和陈旧事件；
- 媒体契约：H5、AI、设备呼叫、微信 VoIP 的 stream ID、编码、采样率和帧长；
- 公共驱动契约：音频 adapter 启停/读写/退出，摄像头帧所有权和无效 adapter；
- 板级静态与主机仿真测试：BK7258、ESP32-P4 4.3、立创·实战派 ESP32-S3；
- 重构约束：仓库布局、构建调度、验证包和板卡 registry。

## 2026-10-22 同组歌曲字幕的旧 Tag 清除

用户日志在 at-ms=29698 收到 `happy final=1`，随后 32444..51020 连续十条
`input=none group=same final=1` 仍保留 Tag，播放快照同时为 `pcm=1`。
`product/tests/test_ai_view.c` 按此顺序回放，修改前在“旧介绍语 Tag 压制歌曲嘴部”
断言失败。修复后，已完成字幕段后的下一条 AI 字幕重新确定 Tag；同一段的局部到
最终字幕仍继承明确 Tag。覆盖固定/缺省 utterance ID、明确 neutral、100 ms 内更新、
独立 Tag 只消费一次、无效消息不消费及 round_end 尾音保留。

`test_ai_face_trace.py` 同时通过真实 runtime 入口验证同组 `tag=1->0 sync=1`，
以及明确 neutral 恢复 Tag。现有波形测试验证 Tag 清除后 S3/P4 恢复 PCM 动效。
沿用并标注 `tools/check.sh` 中产品 ASan/UBSan 与诊断测试入口。用户后续确认歌曲
嘴部已恢复；该次反馈没有附新的固件身份日志，不扩展为全场景实板验收。

后续复查新增 504 组公开解析接口组合：21 种情绪 × 3 种来源（字幕、round_start、
独立 emotion）× 追加/替换 × 有/无 utterance ID × 正常/回绕时钟。
覆盖同段局部到最终字幕、轮末尾音保留、无效情绪的新段恢复默认、100 ms 内强制
同步、新轮次丢弃待关联 Tag 及会话重新初始化。临时源码副本恢复旧的同组保留逻辑
时，新组合测试断言失败；当前生产实现通过。此轮只增强测试和记录，不更改固件行为。

## 2026-10-22 主机测试路径可迁移性

GitHub Actions 在房间键盘测试编译阶段失败，原因是生成的 C 测试写死了开发机的
`/home/workspace/xiaotai`。房间键盘、房间刷新、设置布局和 BK 状态测试改为从各自
仓库根目录传入头文件搜索路径；只调整测试装配，不改变固件行为或测试断言。
现有 `test_room_keypad.py`、`test_s3_room_refresh.py`、`test_ui_alignment.py` 与
`test_bk_settings_status.py` 覆盖此次机械修改，均已由 `tools/check.sh` 执行；
GitHub 的独立 checkout 用于验证不再依赖本机目录。后续 CI 运行暴露 runner 缺少
AEC 策略检查所需的 `rg`，因此在工作流中显式安装 `ripgrep`；继续运行现有 AEC
策略检查验证，不跳过该检查。

## 2026-10-22 AI Tag 来源与轮次诊断

`test_ai_face_trace.py` 通过真实 `handle_ai_command` 的会话/代次/JSON 准入、
产品 JSON 解析器和表情同步入口，先因缺少诊断失败，再验证本条收到情绪、同段保留
Tag、新段/新组/新轮次清除、独立 Tag 被下一字幕消费、轮次结束仍保留当前 Tag、
`neutral` 明确 Tag、无效消息和互斥锁失败。无情绪的重复局部字幕不逐条打印，
字幕节流语义不变；带情绪的输入即使与旧值相同也记录，避免误判为旧 Tag 残留。
陈旧代次在记录之前丢弃。日志只包含固定事件名、标准情绪名、数值/布尔状态，
不输出字幕、utterance/session ID 或未识别的服务器字符串。已注册 `tools/check.sh`。

ESP 日志 `AI face trace` 中 `input` 是本条消息的情绪（`none` 为未提供，`invalid`
为不支持），`tag=A->B` 是协议状态处理前后，`group=new/same` 标记字幕分组，
`pending=1` 表示独立 Tag 等待下一条 AI 字幕，`sync=1/0/-1` 分别表示表情快照
同步成功、失败、此次未请求同步。`valid=0` 表示字幕缺少必需字段；输入中出现
情绪名不等于消息被采用。INFO 日志时间及 `generation/at-ms` 用于对照播放阶段。

该诊断不清除或延长 Tag，也不改变明确 Tag 关闭示波的规则。仍需实板采集从放歌请求
到歌曲开始的连续日志，配合音乐播放中的 `face-animation`，才能区分歌曲本身带 Tag
与介绍语 Tag 保留；主机通过不构成这次实机问题的根因结论。

## 2026-10-22 AI 表情优先级回归

明确的服务器情绪 Tag 控制完整表情并关闭示波；没有 Tag、使用本地默认表情时，
播放驱动嘴部动效并保持眯眼。`happy` 既可能是本地默认值，也可能来自服务器，必须
按 `emotion_tag_present` 区分，不能只比较表情名称。

`test_playback_waveform.py` 在真实 tick 输入片段和生产绘制函数中，先复现 build.35
错误地让 Tag 与示波共存，再验证 P4 与 S3 恢复 Tag 优先、PCM 变化不覆盖 Tag、
清除 Tag 后眯眼与点阵恢复、局部像素更新、停止播放和预览隔离。
`product/tests/test_ai_view.c` 通过真实 JSON 入口验证默认 happy → 服务端开心 Tag →
下一条无 Tag 回复，保证默认表情不伪造 Tag、旧 Tag 不跨回复继承。
两者均由 `tools/check.sh` 执行。只读 `face-animation` 诊断保留；BK 原 Tag 策略符合
要求，无需同步 build.35 的错误改动。新固件仍须实屏验证有/无 Tag 的播放切换。

## 2026-10-22 M5 放歌嘴部动效取证（历史诊断阶段）

用户 build.33 日志显示 AI 播放期间解码/I²S 错误为零、动效开关开启，但嘴巴固定。
UI 慢日志不能区分表情 Tag 抑制、PCM 可视数据过期或七列高度不变，暂不认定根因。
`face-animation` 新增只读 `Face inputs`：AI 状态、phase、Tag、表情、PCM 可用性
和七列高度（十六进制，每位 0–7）。必须在播放中采样，连读几次比较 columns；
各公开快照非原子组合，会话切换时应重读。`pcm=1` 只表示可视采样有效，
不表示非静音或页面确实绘制；`tag=1` 仅在 AI 活跃时参与表情优先策略。

`test_playback_waveform.py` 先因缺少诊断输出失败，再验证 TAG 与有效 PCM 同时可见、
满幅七列值、待机、关闭动效、无效命令及查询不修改播放状态。沿用 `tools/check.sh`
注册；生产变更仅增加按需诊断，不修改动画优先级、刷新频率或音频参数。
嘴巴固定的问题仍须新版播放中取证，不能将诊断测试通过当作修复。

## 2026-10-22 连续操作与异常注入补测

- 产品核心 `test_ai_view.c` 从真实字幕 JSON 入口遍历 ASCII、双字节、中文、
  四字节字符，以及 0–255 字节前缀的替换/追加组合，共 2,048 组。
  新用例先复现长中文字幕恰好填满时多丢一个完整字符（预期 255 字节、实际 252），
  修正 UTF-8 截断边界后通过。另回放 10,000 轮开始、局部字幕、完整字幕、结束和
  跨会话事件，每轮验证历史顺序、最近 12 条上限及局部更新不重复追加。
  两项随 `tools/check.sh` 的产品核心 ASan/UBSan 和覆盖率检查执行。
- `test_preferences_persistence.py` 新增 1,000 轮连续修改、存储失败和合并重试，
  并在七个 NVS 写入边界分别插入重置请求，验证取消重置后仍保存最新修改。
- `test_bk_acknowledgement.py` 对试听的 15 个音频块逐个注入写入失败及取消，
  覆盖启动前取消、等待超时、信号量初始化失败和重试；检查缓冲区只释放一次、
  旧 worker 退出前不能抢占、退出后可再次试听。测试使用主机 RTOS/驱动替身，
  不证明真实线程调度、DMA 或音质通过。

验证结果：`bash tools/check.sh` 退出 0；430 项工具单测、产品核心 ASan/UBSan、
五板 manifest 校验及 BK 专项契约检查通过。产品核心行覆盖率 93.55%、分支 65.45%。

本次仅共享字幕截断逻辑发生生产行为变化，不调整 M5 声学或 P4 视频参数。
补测完成时已有固件不包含该修改；后续 M5 已重建为 build.33，尚未烧录。
各板最新构建与实板状态以各自验证记录为准，字幕显示仍需同版实屏验证。

## 2026-10-22 设置与交互缺口回归

- `test_preferences_persistence.py` 由真实 ESP 保存调度/worker 入口注入 NVS
  打开、逐键写入、提交和任务创建失败，先复现写失败后仍提交，再验证失败状态、
  快照隔离、连续修改合并、重置拒绝在途写入及清除后不再提交。
- `test_bk_product_gaps.py` 先复现表情背景点击误应用、分页缺少边界和 AI 菜单回到
  首页，再验证 21 项预览/明确应用、联系人三项分页及空隙、存储失败状态、历史字幕
  分组/跨会话/容量与 ASCII/UTF-8 完整绘制、异步字幕不覆盖用户页面。
- `test_bk_acknowledgement.py` 先因缺少试听入口失败，再验证男女样本、单次试听、
  原验证码三次播报、占用/闭扬声器拒绝、分配/任务/驱动失败与缓冲回收。
  `generate_bk_prompt_assets.py --check` 检查已提交 PCM 与共用 WAV 一致。
- 现有 `test_contact_pagination_layout.py` 先复现列表直接拨号，再锁定仅进入详情；
  UI 对齐和绑定完成回放继续覆盖提示/触摸及播报取消。BK `test_storage.sh` 增加
  旧五/六字节记录迁移、新回应声/表情持久化及无效表情拒绝。

上述均注册统一入口。主机测试不能代替新固件的掉电/重启、实屏操作、扬声器听证、
反复业务切换和长稳。M5 声学问题、P4 视频参数及 ATK 固件不属于本轮修复。

## 测试驱动开发约定

S3 编码期间状态变化：已注册的 `test_s3_opus_media.py` 在真实 worker 的编码边界
注入闭麦、mute epoch、会话代次、业务模式、上行授权、PTT 松开和连接就绪撤销，
对 A-law/Opus 两种分支共 14 组场景先失败再修复。编码完成后重新检查，失效帧不提交
SDK、不误报编码失败，队列槽正常归还；这不代替真机并发切换和闭麦验收。

S3 纯音频通话：`test_s3_audio_calls.py` 分别按 M5/立创定义执行能力上报、
呼叫选择/接听、媒体视频策略、SDK 发送与联系人渲染入口。先复现旧实现声明/发送视频
及显示视频按钮，再验证上下行 `none`、`no_video=true`、外呼 voice、旧视频状态拦截，
同时保留 H5 MJPEG。已注册 `tools/check.sh`。主机回放不证明服务端已更新能力；
新固件仍须重连上报并验证微信主被叫双向音频。

CoreS3 文本内存：`test_cores3_text_memory.py` 执行各板真实组件 CMake 入口，
编译生产字幕数组及历史追加函数，检查编译后段归属、容量、去重、截断和清空复用。
先复现 M5 两个屏幕变体仍占内部 BSS，以及关闭外部 BSS 时未拒绝构建；
再验证仅 M5 的 4352 字节文本移入外部段，立创 S3/P4 仍使用内部 BSS。
已注册 `tools/check.sh`。主机属性桩不模拟 PSRAM/cache；还须核对原生 ELF，
并实板验证字幕、设置保存、解绑及媒体并发，不能据此声称 CPU 或 AEC 已改善。

CoreS3 缓存行对照：`test_cores3_cache_experiment.py` 经检查器 CLI 验证最终
sdkconfig，只允许 32 B → 64 B 缓存行及其派生值变化，拒绝旧配置未生效、
扩大缓存容量、混改音频/优化选项、缺失或冲突配置。先在旧默认配置上失败，
再修改 M5 默认值；它不证明性能收益。构建后还须用真实 baseline/candidate
sdkconfig 运行 `tools/check_cores3_cache_experiment.py`，并进行实板对照。

S3 摄像头与方向由 `test_s3_h5_cadence.py`、`test_camera_rotation_report.py`
覆盖：H5 目标节拍、通话/AI/房间不采帧、编码期间挂断不发旧帧、H5 方向保持。
`test_default_video_call.py` 和 `test_incoming_call_media.py` 验证 S3 语音选择、
来电与显式接听，以及 P4 原有视频策略。实板仍须验证两种呼叫方向、双向音频、
拥塞、挂断重拨和 H5 切换；原微信单向视频候选已按产品要求撤回。

UI 周期预算：`test_ui_status_budget.py` 回放产品 tick、触摸、控制台与诊断入口，
先复现普通 UI 重复扫描任务栈，再验证常规读不扫描、诊断读仍返回真实水位。
`test_p4_ui_video_timer.py` 先复现空闲时重复清理，再验证无视频暂停、视频就绪恢复、
挂断/离页一次清理及重新进入恢复。两项已注册统一检查；主机测试不证明真机耗时。

UI 耗时观测：`test_ui_refresh_observation.py` 在生产 tick 的快照边界注入等待，
验证总时间被拆分到正确阶段；回放实际 LVGL monitor 回调，验证局部/全屏像素统计、
窗口清零、限频和原回调保留。先复现旧日志没有阶段区分且缺少渲染观测，再添加探针。
`test_s3_display_geometry.py` 同时验证在持有 LVGL 锁时安装探针。已注册统一检查；
这些是诊断准确性测试，不是卡顿修复或实板吞吐证明。

`test_task_diag.py` 回放实际串口命令与 tick ISR，验证每核采样归属、固定窗口、
缺失任务归入其他而不误读当前任务、停止后的迟到回调、重复调用清零及部分注册失败清理。
先因缺少入口失败，新增后通过并注册统一检查。S3 使用官方 hook，不改变 TiRTC SDK
要求关闭 trace 的配置；这些测试验证观测行为，不证明实板 CPU 占用或卡顿根因。


统一 AI 格式：`test_ai_alaw_rollout.py` 回放 P4/BK 生产采集与编码 worker、
BK AI 媒体准备和 SDK 发送入口，验证 16 kHz DSP、8 kHz/160 字节上行、
旧代次/静音/错误长度拦截、初始化失败清理及 P4 预录音尾段衔接。
还覆盖编码期间静音/代次变化不得发送、正常丢弃不得误报编码失败，
以及 BK AI→8 kHz 通话→AI 的 DSP 与包长、静音 A-law 零值和发送门禁。
先复现两板仍执行 Opus 编码及 P4 丢尾段，再修改实现；已加入 `tools/check.sh`。

产品协议和业务场景回归验证 alaw 上行 / Opus 下行的请求、响应与独立方向合同；
S3/M5 编码、预录音、Opus 下行继续由 `test_cores3_ai_g711.py` 覆盖。
共享 FIR 的迁移保持原 API，既有 `test_audio_resampler.sh` 覆盖分块连续性和抗混叠，
没有为文件移动添加自证测试。BK 和 P4 原有播放、结束排空及会话测试继续执行。
四板默认配置会通过原生构建后的 sdkconfig 核验；主机测试不代替各板的声学验收。


`test_cores3_ai_g711.py` 驱动生产 start_session 和响应格式校验、TiRTC SDK
发送入口、编码 worker、预录音到实时音频衔接及下行播放。先复现 AI A-law 被拒、
仍发送 Opus/16 kHz 和预录音采样数不符，再验证上行 8 kHz / 160 字节 / 20 ms、
下行保持 Opus / 16 kHz；默认 Opus 路径继续由 `test_s3_opus_media.py` 保护。
已注册 `tools/check.sh`。这些测试不证明服务端已接受非对称格式或实板处理耗时下降。

build.17 增强同一已注册测试：实际 start_session 发送入口按官方 AI 协议使用
`alaw`；生产 CONNECTING 响应分支先复现拒绝合法 `alaw/8000`，再验证修复后
兼容 `g711a` 别名。错误采样率、声道、下行编码、缺失字段与旧请求不能准入；
失败诊断只打印白名单编码和数值，不泄露会话 ID、token 或未知字符串。


`test_s3_audio_observation.py` 执行生产采音边界采样、32 块聚合、公开快照和
控制台格式化，覆盖排队未播、DMA 尾音、会话换代、计数重置、时钟回绕与不支持观测。
CoreS3 探针测试执行回调注册、溢出计数及非消耗读取，验证错误传播；
原实现因缺少观测入口失败，新增后通过，已注册 `tools/check.sh`。
现有 voice-diag 回归同时保护命令时长、只读行为及首尾事件输出。
主机测试不模拟真实 DMA/调度，不证明重复说话或 AEC 超期已经解决。


`test_camera_rotation_report.py` 回放生产能力上报、发起设备呼叫和接听信息提交，
从 M5 工程的编译定义读取方向，解析真实提交的 JSON。旧实现分别因缺少方向、
沿用 270° 失败；修正后 M5 三个场景及两个呼叫入口均为数值 180，语音能力不变。
同一测试验证立创 S3 和 P4 保持原有字段和值，已注册 `tools/check.sh`。
远端实际显示方向仍需升级后重新打开 H5 实测。

CoreS3 功放标定回归执行生产 `audio_codecs_init()`，在 AW88298 创建边界验证
乐鑫官方 CoreS3 BSP 的 15 dB PA 增益补偿，同时保护 MIC1/MIC3 增益和共享时钟初始化。
旧代码因为省略 `hw_gain` 而失败；补齐后通过。用例加入已注册的
`test_cores3_board.py`，原生构建也执行。它证明标定配置与官方一致，不证明实板
削顶、处理超期或双讲已经解决；仍需当前固件的声学验收。

CoreS3 摄像头在顶部的 180° 方向通过 `test_s3_display_geometry.py` 回放生产
LVGL 注册与触摸配置。先在原方向断言失败，再验证两轴镜像且不交换 XY；
按锁定的 esp_lcd_touch 1.2.1 `max - raw` 语义验证四角和中心，坐标不越界。
同一测试继续保护立创原方向。实板仍需检查屏幕朝向和四角点击。

S3 显示几何回归：`test_s3_display_geometry.py` 执行生产 `lvgl_init()`，
在 LVGL 注册边界按实际板卡的原生尺寸验证地址范围、四角和方向。
先复现 CoreS3 被共享代码强制交换 XY 后，240×320 面板地址范围与 320×240 UI 不符；
改为板级方向参数后通过，并保留立创 ST7789 交换 XY / 水平镜像的既有行为。
已注册 `tools/check.sh` 和 CoreS3 原生构建检查。实板仍须验证完整页面、颜色和四角触摸。

CoreS3-Lite PSRAM 启动回归：`test_cores3_memory_config.py` 先在当前默认 Octal
模式上失败，再验证 Quad 默认值及生产 CMake 检查入口对旧配置、关闭 PSRAM、
冲突配置的拒绝。真实 `idf.py reconfigure` 也用保留的 Octal `sdkconfig` 验证拒绝，
避免只改 defaults 而继续编出错误镜像。已注册 `tools/check.sh`；
这证明配置约束，Quad 固件启动、8 MB 内存识别与内存测试仍须同版实板验证。

CoreS3-Lite：`test_cores3_board.py` 先在缺少板卡、供电入口及音频槽位适配时失败，
新增实现后验证显式 C/E 屏幕变体、独立版本来源、MIC1/MIC3 压紧、摄像头电源位保护、
错误传播及面板指令隔离。设备能力上报用 HEAD 原实现重放，先复现错误的立创板名；
触摸板按键启动先复现缺失成功入口，再验证不占 GPIO0 且不阻塞控制启动。
已注册 `tools/check.sh`，也由新板原生构建执行。S3 媒体源码移入平台目录属于行为保持
重构，既有 `test_s3_*`、音频契约和主机媒体测试继续通过原兼容路径覆盖。
ESP-IDF 版本文档由板卡清单、`tools/setup_esp_idf.sh` 和实际构建描述符交叉核对；
不为文案新增自证测试。实板仍需核对 PCB/屏幕、音视频、双讲和全部业务流程。

2026-10-17 ESP32 双附件交付：`test_esp_merged_firmware.py` 从分文件 ZIP 调用
公开合并入口，覆盖 S3/P4 不同引导地址、P4 OTA 数据、原始 ZIP 不变、两附件校验清单、
损坏输入、区域重叠、错误合并结果与工具失败。先在缺少合并入口时失败，实现后通过，
已注册 `tools/check.sh`。主机测试使用外部 esptool 进程替身；真实发布包还须使用
ESP-IDF 环境中的 esptool 合并并逐段校验，同版网页烧录和启动由实板验收确认。

三板发布身份：`test_firmware_versions.py` 从构建入口验证编号分配、失败重试、
dry-run 不修改编号、并行构建互斥及原生 CMake 嵌入和依赖重配置。
`test_esp_package_identity.py` 验证实际 BIN 描述符、ELF 与构建号一致，拒绝旧包。
`test_release_identity.py` 从验证包生成三板统一标题，拒绝缺少构建号或校验损坏的包；
均由 `tools/check.sh` 执行。版本命名不替代音频、UI 等实板验收。

2026-10-16 BK 音频争锁与发布日志：`test_bk_ai_playback.py` 通过实际 Opus
入队入口复现短暂锁占用直接丢包；现在最多等待一个 SDK tick（本工程 1 ms），
超时仍返回 BUSY，累计 `busy` 单独记录，不再用 overflow=0 推断无丢包。
`test_bk_release_traces.py` 回放实际心跳任务与 metrics 入口，验证 INFO 保留
心跳发送和指标样本、关闭原始调试打印，DEBUG 保留取证。均注册 `tools/check.sh`。
这不证明实板音质改善或排除 DAC 削波；需要同版连续播放、双讲和结束尾音听音验收。

2026-10-16 BK 彩色点阵嘴：`test_bk_ai_face_animation.py` 通过生产公开
`xiaotai_ui_update_ai_mouth()` 验证与 S3 相同的青、绿、暖黄 RGB565 像素及
高字节在前的面板格式；旧实现因底行颜色不符失败。保留 66×36 嘴部更新、
相同输入不刷新、禁用动效不刷新及缓冲区边界检查；沿用 `tools/check.sh` 注册。
实板仍须确认面板颜色、Tag 优先级及播放时嘴部动效。

2026-10-16 三板尾音复查：P4 `test_p4_ai_playback.py` 与 BK
`test_bk_ai_playback.py` 在生产公开状态入口注入最后一次写入完成的线程交错，
旧实现均误判排空，修复后保留 worker 所有权或最新硬件尾部期限。
BK `test_bk_disconnect_playback.py` 另覆盖没有 end_session、正常关闭回调时
仍播完剩余 7 包。上述测试及 S3 尾音回归均由 `tools/check.sh` 执行。
主机回归不执行真实 DMA，三板最终一句完整性与语速仍需同版实板听音验收。

2026-10-16 S3 尾句排空：`test_s3_playback_drain.py` 通过生产入队、sink、清队列和
公开 status 回放 19 包尾音。旧实现报告 pending=0；修复后队列和 worker 持有帧
均计数，溢出不虚增、清队列不漏减，PCM 累计时长和 DMA 尾部估算可见。
回归还覆盖最后写入与 status 读取交错、启动输出锁超时及旧 worker 持有帧；
输出和统计复位完成后才发布媒体激活，防止新帧先写入又被复位清掉。
另回放用户 generation=4 的直接断连：剩余 7 包、arrival-grace=0；通过实际
runtime 排空入口确认 7 包和 DMA 尾部完成前不能调用 finish_session。
`test_s3_opus_media.py` 同时验证实际解码/写入路径更新时长，AI 320 点仍输出
320 个 16 kHz stereo frame。两项均注册 `tools/check.sh`。

DMA 估算使用板级既有 6×240 frame 容量，完整写入后最多等待 90 ms；它不是
实际 DMA 水位。主机测试不能证明用户反馈的尾句语速变快已解决，仍需同句录音、
结束阶段日志和新版 status 对照，检查是否截尾、语速变化及重复唤醒恢复。


2026-10-16 项目复查修复：`test_room_async_recovery.py` 覆盖公共房间 HTTP 完成事件丢失、
独立请求编号、连接/加入超时、租约检查、重复加入与带版本的关闭通知；同时重放 BK
生产回调中的分配及队列投递失败。`test_esp_signal_recovery.py` 覆盖 MQTT 必需订阅确认、
订阅失败恢复及 ESP 平台关闭房间。`test_esp_package_identity.py` 覆盖 ESP 镜像/ELF/版本/芯片
身份校验和统一打包 CLI 的验证状态透传。均先在旧实现复现失败，已登记 `tools/check.sh`。
公共房间的真实异步回调场景也加入 `product/tests/test_room_resilience.c`。
问题背景、修复机制与证据边界见[二次开发易错点](../docs/development/pitfalls.md)。
本轮主机回归不能替代 BK/S3/P4 的弱网恢复、重复进出房间和长时间媒体并发实板验收。

2026-10-16 BK7258 功能复查补充 `test_bk_close_reconnect.py`，通过生产 SDK
断开/接入回调、关闭通知入口和媒体 worker 回放五种模式各 100 轮关闭与重连。
旧实现先在“关闭待处理时 busy 必须保持”断言失败；现在传输句柄清空前发布关闭
屏障，旧音视频清理期间拒绝新接入，清理完成后、业务通知前恢复准入，避免影响
AI 转拨或待接听来电。覆盖重复/陈旧回调、唤醒队列失败重试及清理中接入。
已注册 `tools/check.sh`；主机桩不执行真实音频 DMA、SDK 网络线程或 SRAM
独占监视器，候选固件仍需按 `HARDWARE_ACCEPTANCE.md` 完成实板功能验收。

2026-10-16 build.48 实板确认：控制队列入口 PRIMASK=0，解锁后仍为 1，
PendSV pending；同一个事件节点在等待到期前重复入链。build.49 仅将每轮
轮询执行 CAS 的 `s_pending_close_state` 移至 SDK `.sram_spinlock_section`。
`test_bk_room_close_callback.py` 新增目标 GCC 编译实际入口/声明的段归属检查，
先因 `.bss` 失败再通过；既有交接测试覆盖一次通知、队列失败重试、消费后复位。
此测试已由 `tools/check.sh` 注册，证明存储布局与交接合同，不模拟 SRAM
独占监视器。build.49 交付后用户反馈 uptime 已达 21 分 38 秒，所贴约 15 分
50 秒待机窗口无重启且内存稳定；写丢失的底层硬件因果和负载稳定性仍未直接验证。

2026-10-16 build.47 实板反复失败：控制 TCB `0x60557c50` 的事件节点
`0x60557c68` 前后指针都等于自身，事件链表 count=2；状态节点仍在延时链表，
唤醒 tick 比冻结 tick 大 100。AP1 阶段停在 vListInsert，AP0 等内核锁，随后
CP 的 8 秒心跳保护复位。HTTP 200、SDK start rc=0 不能证明调度正常。
`test_bk_event_reentry.py` 用实际 SDK vListInsert/vTaskPlaceOnEventList 重放重复
入链，复现相同自环；第一次运行因没有保存首次重复入链而失败，加入取证后通过。
它只证明自环机制和取证有效，尚未复现让阻塞任务再次运行的上游原因，不能算修复。

build.48 开发取证增加 `ap-first-reentry`（首次检测到已入链的事件节点）与
`ap-queue-boundary`，首次异常后冻结该核的队列边界记录。保持原队列/锁行为，
不跳过重复插入、不提高心跳超时。事件概要分两行，修正旧日志被 UART 截断。

- first-reentry 的 16 个 word 按 offset 顺序为：有效标记、TCB、目标事件链表、
  插入前 count、tick、唤醒 tick、原事件 container、原状态 container、PRIMASK、
  IPSR、BASEPRI、FAULTMASK、临界嵌套数、保存的中断状态、scheduler suspended、ICSR。
- queue-boundary 每 8 个 word 为一个阶段（0=API 入口、1=yield 前、2=解锁后）：
  queue 地址、PRIMASK、IPSR、BASEPRI、FAULTMASK、ICSR、临界嵌套数、保存的中断状态。
  记录按核保存；若任务迁核或其他任务覆盖，必须用 queue 地址核对，不能拼成同一任务轨迹。
- 下一次实板需要从启动到首次心跳超时的日志，重点保留上述两种记录，以及
  ap-port、ap-event-tail。尚未完成实板验证；不能将 build.48 标记为重启修复版。

2026-10-16 build.46 实板：两次 CP 快照 tick=5179，kernel owner=1/count=1；
TCB0=显示线程、TCB1=控制线程。LR 对应 xQueueSemaphoreTake 和
xQueueReceive→vTaskPlaceOnEventList；LR 是调用返回地址，不能当作当前 PC。
临界记录深度为 1/2，nesting 读值为 0/0，尚不能据此判定链表循环或内存破坏。
官方 .8→.10 的 spinlock.c/h 确有改动，任务/队列源码相同不代表整个锁路径相同。
build.47 增加仅开发启用的内存阶段记录与有地址边界的六字读取，不修改锁算法。
`test_bk_scheduler_snapshot.py` 先在真实 vTaskPlaceOnEventList 的锁入口断言失败，
再验证两个核的锁等待、事件插入、延时链表更新、解锁阶段，以及实际 port 入口的
嵌套锁计数和中断恢复；它不模拟硬件独占访问或证明重启已修复。
`test_bk_critical_lr_snapshot.py` 校验 CP 打包需要新增 AP 符号并拒绝陈旧地址。
下一次实板须保留 ap-port、ap-event、ap-event-memory 和 ap-lr 全部超时行。

2026-10-16 BK7258 build.44 版本误判纠正：同一 AP ELF 同时含 SDK 的
`1.0.0+build.42` 和产品的 `1.0.0+build.44`。SDK 的 APP_VERSION 在 CMake
配置时读取环境，增量构建未重新配置，旧的启动版本不能用来断言用户烧错文件。
`test_boot_identity.py` 新增三个失败用例：新构建未使 SDK 配置缓存失效、
finalize 放行混合版本、finalize 放行新 ELF 配旧烧录内容。现在构建号变化会
重新配置 AP/CP，并校验 ELF 版本唯一、整包剥离 CRC 后的 AP/CP 内容与 BIN
一致。启动身份改用普通 raw 输出，避免非阻塞启动输出丢失。
build.46 开发镜像启用临界区 LR 记录，CP 超时快照从本次 AP ELF 获取地址，
再次打包后验证地址契约并恢复 SDK 源码；release 关闭该记录。
build.44 的频繁重启仍是有效的实板失败证据。主机回归只能证明身份校验和
诊断构建正确，不能证明 AP 停顿已修复；需采集 build.46 首次超时的 ap-lr 输出。

2026-10-16 BK7258 build.41 重启复查：实板除 IPC 心跳超时外，还捕获
CP `pbuf_free` 释放 AP SRAM 地址导致的 `os_free_debug` 断言。
`test_bk_wifi_tx_ownership.py` 通过实际 SDK netif 发送入口，先复现两次异步
发送复用同一描述符，再验证每次发送独立复制、描述符初始化、源 pbuf 不变以及
分配/发送失败引用平衡；命令确认用例通过真实收发入口，先捕获 TX 插入列表未持有
RX/超时路径共同使用的 mutex，再验证确认、超时与信号量初始化失败。
`test_bk_mqtt_eof.py` 先以 `EOF busy loop detected` 复现 MQTT 对关闭连接持续
读取，再验证 EOF/半帧立即退出、超时仍作为空闲、完整帧和分段输入正常。
这些是已复现的代码缺陷；尚未证明它们解释全部 AP SMP 停顿，必须核对新镜像
的实板复位频率。CP `DEBUG-bk-pbuf-owner` 仅在异常跨堆释放时增加直接调用者诊断。

2026-10-16 BK7258 构建身份：`test_boot_identity.py` 先因启动日志只有产品
版本、没有单次构建号而失败；后来又因误用日期构建号而失败。现在构建脚本
把工程内 `BUILD_NUMBER` 的数字递增，AP 启动时打印完整的
`1.0.0+build.n` 和 `build=n`，构建记录和验证包清单将其绑定到最终 `all-app.bin` 的
SHA-256。测试用临时镜像验证头文件、构建记录与 SHA-256 一致，并要求最终
AP ELF 包含同一构建号。仍需在实板启动日志中核对 `build=` 与准备烧录的
清单，才能确认板上运行的是这份镜像。

同一工程 `package/` 中的正式烧录副本使用 `1.0.0+build.n` 命名；SDK 原生
`all-app.bin` 仍用于 SDK 打包流程。`test_boot_identity.py` 先因没有唯一命名
副本失败，再验证同目录副本与原生整包逐字节一致。构建身份头文件和记录直接
放在已有构建根目录，不再新建身份子目录。

2026-10-16 BK7258 版本上报与产物整理：新增回归先复现平台上报仍为
`1.0.0`、打包文件名缺少 build 号和输出目录缺少用途说明，再通过编译实际
平台 profile JSON 验证其使用生成的完整版本。验证包测试通过公开打包入口核对
文件名、清单版本、SHA 和构建配置一致，并拒绝 profile 不匹配的产物。
本地开发使用默认工程目录，只有线上发布候选使用 SDK 的 `_release` 目录；
用途记录在产物目录的 `README.md`。测试均注册在 `tools/check.sh`。

2026-10-15 BK7258 跨核 Wi-Fi 发包缓冲区：现场 CP `core_thread` 在
`rwnx_start_xmit` 读取 `skb->p` 时发生非对齐访问，寄存器中的指针已被数据覆盖；
同时可见 `bam_set_baw_state` 断言和 AP 心跳超时。官方 SDK 的
`lwip_psram_example` 在 AP/CP 两侧启用 `CONTROLLER_AP_BUFFER_COPY`，并在
AP 启用 `LWIP_MEM_LIBC_MALLOC_USE_PSRAM`。`test_bk_cp_tx_buffer_copy.py`
先在现有配置失败，再检查两侧配置和已编译 CP 公共入口
`cif_handle_txdata` 确实先复制 AP pbuf、归还 AP 缓冲区，再排入 CP Wi-Fi
发送队列。该主机检查只证明跨核缓冲区隔离路径已编入固件；心跳超时是否消除，
仍须刷入同一镜像做空闲长稳、网络重连和呼叫实板验证。

2026-10-15 后续实板反证：启用官方 AP 缓冲区复制的镜像仍多次在 AP 最后
一次 `tx-exit rc=0` 后停止心跳，CP 在约 8 秒后断言重启；停止前堆空闲量稳定，
且故障可在启动几秒或空闲数分钟后出现。当前日志不能判定 AP 是被硬件停核，
还是两个核的软件调度停顿。`test_bk_heartbeat_trace.py` 先因分析器不能关联
CP 的 AP 硬件状态快照失败，再验证同一次启动的超时记录含 halt、断电状态，
并在下次启动清空旧快照。CP 只在心跳超时断言前读取 SYS 的运行状态及两颗
AP 核控制寄存器，不修改寄存器。刷入新镜像后须收集
`[DEBUG-bk-hb] ap-hw` 行；即使显示双核仍运行，也还需要 AP 停顿现场的 PC/
调用栈才能定位具体代码。

2026-10-16 BK7258 心跳判超回到 8 秒：官方 SDK 3.1.1.10 在未启用
`CONFIG_WDT_EN` 时按 2 秒心跳的 3 个周期（6 秒）判超；10 月 2 日所用
3.1.1.8 为 8 秒。`test_bk_heartbeat_timeout.py` 先在未接入补丁时失败，再把
项目补丁应用于 SDK 原始 CP 源码，并编译执行无 WDT 的超时表达式，验证其为
8000 毫秒。只延长 CP 判超窗口，不改变 AP 发心跳周期；AP 停顿原因仍待实板定位。

2026-10-15 BK7258 超时现场 AP 状态：`test_bk_heartbeat_trace.py` 的新增用例
先复现分析器遗漏 CP 采集的两次 AP SRAM 快照，再验证同一超时记录保留
系统 tick、当前任务指针及 shell、mailbox、内核自旋锁的 owner/count，
并在下一次 AP 启动时丢弃旧快照。独立的 `tirtc-prestart-locksnap` 镜像
仅在 CP 心跳超时路径隔 20 毫秒读取两次共享 SRAM；原始 SDK 源文件已恢复。
主机测试和编译只能证明采集代码与解析逻辑可用，现场快照和故障根因仍待实板验证。

2026-10-16 BK7258 临界区调用地址诊断：`tirtc-prestart-locksnap` 实板现场
显示 AP core1 持有 `xKernelLock`，20 毫秒内 AP tick 未变化。新的隔离镜像启用
SDK 的 `CONFIG_CRITICAL_LR_RECORD`，并在 CP 超时路径读取双核嵌套临界区调用
地址及最近进入地址。`test_bk_critical_lr_snapshot.py` 通过诊断工具的命令行
验证 AP ELF 符号契约、CP 插桩、地址变化拒绝及 SDK 源码恢复；旧 AP ELF 因缺少
LR 符号被拒绝，新镜像经双核 Release 编译。主机测试不能证明实板能捕获持锁
调用点，且该 SDK 选项增加临界区开销；仍需烧录并保存首次超时的 `ap-lr` 行。

2026-10-16 BK7258 媒体工作线程 SRAM 候选：实板 `prestart-critical-lr` 镜像在
约 10 分 46 秒再次发生 IPC 心跳超时，CP 两次快照均显示 AP core1 持有
`xKernelLock`、tick 停在 `640099`，进入临界区的地址对应 `xQueueReceive()` 与
`vTaskPlaceOnEventList()`；当前任务 TCB 位于 PSRAM，但快照还不能确认它就是
媒体线程，也没有实时 PC。`test_bk_room_close_callback.py` 通过生产
`start_media_worker()` 验证媒体线程使用 SDK 的内部 SRAM 建线程接口、保留原优先级
与 4096 字节栈，并覆盖启动失败时的队列清理；测试先在 PSRAM 实现上失败。
该改动是待实板验证的单变量候选，主机检查和编译均不能保证连续运行几十分钟。

2026-10-14 BK7258 周期指标的 SMP 锁：`test_bk_metrics_no_kernel_scan.py`
先因统计任务调用 `uxTaskGetSystemState` 失败，再验证统计仅读取任务数。
SDK 实现的全任务扫描持有 `xKernelLock`；现场曾在
`metrics-task-scan-enter` 后失去 AP 心跳。CPU 使用率与最低任务栈水位
现标记为不可用，堆、任务数、RSSI 和网络计数继续输出。已纳入统一检查；
实际重启是否消除仍需相同镜像的实板长时间验证。

2026-10-14 BK7258 空闲 Wi-Fi 查询：`test_bk_rssi_cache.py` 已注册
`tools/check.sh`，先复现首页/网络页周期渲染直接调用同步跨核
`STA_GET_LINK_STATUS`，再改为读取缓存。实板连续串口采集又在周期
`metrics-wifi-enter` 后捕获心跳超时，因此测试进一步通过公开的
`xiaotai_metrics_start()` 驱动一次指标采样，先因同步 Wi-Fi RPC 失败，
再验证周期任务不发起该 RPC。断线事件仍立即失效缓存；当前板卡没有
异步 RSSI 来源，界面暂显示未知信号。其他停点也曾出现，主机测试不能
证明 IPC 心跳重启已消除，需同一板卡新镜像空闲长稳与房间进出验证。
同一回归测试还先复现了 RSSI 未知时网络页误报断网，以及断线后沿用
旧 IP 的问题；现在连接状态依据实际网络事件，信号未知时单独显示未知。

2026-10-14 启动身份日志：`test_boot_identity.py` 已注册 `tools/check.sh`，
先复现 BK 启动无产品版本、ESP32 启动标记不统一，再验证 BK 从
`VERSION.md` 注入版本，P4 与两种 S3 从应用描述符读取版本，并在业务初始化前
输出 `XIAOTAI_BOOT`、工程、SDK 和复位原因。主机检查与四个工程编译验证
只能确认代码和镜像内容；串口实际打印仍需各板实测。

2026-10-14 BK7258 房间退出心跳回归：`test_bk_room_close_callback.py` 已注册
`tools/check.sh`。测试先复现 TiRTC 断开回调执行串口日志，再验证正常房间关闭
只清理连接状态并用原子槽向媒体工作线程投递关闭事件；工作线程定时取走事件，
断开回调不再调用 RTOS 队列 API。`bash tools/check.sh` 与 BK
release 编译已通过；连续进出房间及退出后至少 30 秒无 IPC 心跳超时仍须实板验证。

2026-10-15 BK7258 空闲 IPC 心跳回归：相邻版本实测 `2447246` 在 5 分钟内
0 次超时，`5eaec6e` 在 124 秒内 7 次超时。后者让媒体工作线程每 20 毫秒
唤醒并读取 SMP 队列。`test_bk_room_close_callback.py` 新增队列边界仿真，
先因缺少受控等待入口失败，再验证 8 秒空闲窗口最多 40 次队列读取、
关闭回调事件最多等待 250 毫秒。BK7258 适配层现将轮询改为 250 毫秒，
仍需刷入新镜像验证启动与房间退出；相邻版本对照只能证明回归关联，
不能单凭日志断言 SDK 内部的最终卡锁点。

2026-10-15 实板补充：250 毫秒轮询镜像在 READY 状态约 56 分钟后再次发生
`IPC[1]heartbeat timeout`，重启后进入 AI、收到首帧下行音频附近又发生一次。
因此 250 毫秒仅降低了重启频率。`test_bk_room_close_callback.py` 现先复现
媒体线程持续超时读取队列，再验证媒体线程阻塞等待、SDK 断开回调只写原子槽、
产品控制线程在回调栈外发送一次唤醒事件，并覆盖队列满后的重试。修复候选
已通过主机测试和 BK release 编译；空闲长稳、AI 双向音频和房间退出仍需
同一固件实板验证，不能据此宣称心跳问题已根治。

2026-10-15 后续实板日志确认上述阻塞等待候选仍出现 IPC 心跳超时：H5 STREAM
关闭并完成本机清理后重连时复现，另一次停在 `metrics-log-enter` 与实际
`METRICS` 输出之间。SDK 的普通 shell 日志在缓冲区满时会等待信号量，
而周期指标和 TiRTC 回调都经 `xiaotai_log_write()` 使用该模式。
`tools/test_log_levels.sh` 已由 `tools/check.sh` 注册；本次先使它在公开日志
接口上因使用普通日志模式失败，再验证改用非阻塞输出且保留级别过滤、
格式化内容。日志拥塞时允许丢弃一条日志。这个测试不能证明其他 SDK
锁点已经排除；H5 查看、AI 对话及空闲长稳仍需实板复验。

2026-10-15 BK7258 一次性定位镜像：实板既在 H5 STREAM 启动后，也在 READY
空闲约 100 秒后发生 CP `IPC[1]heartbeat timeout`；另一次报
`os_free_debug: invalid heap pointer 0x2801aec8`。该地址落在当前 AP
链接图的 lwIP 静态堆内，但尚缺释放调用者证据，不能据此修改所有权逻辑。
SDK 补丁仅以 `[DEBUG-bk-hb]` 记录 AP 发送前后序号与返回值、CP
超时时收到的心跳总数，以 `[DEBUG-bk-lwip-free]` 记录 CP lwIP 非法释放的
指针、调用地址与 CP 堆起点；CP 中断接收路径只增加计数。
`test_bk_heartbeat_trace.py` 先复现分析器缺少这些字段及非法释放未判失败，
再验证每次启动独立归因。完成源码检查与双核 release 编译后，仍需同一镜像
实板捕获一次完整故障日志，才能根据 AP/CP 发送接收状态和 CP ELF 调用地址
确定修复点。

2026-10-15 后续实板捕获：READY 空闲时 AP 第 226 次心跳发送成功且 CP
累计收到 226 次，之后 AP 不再发送；另一次启动只发送了第 1 次。另一轮
空闲超过 18 分钟后启动 H5 音视频，分别触发
`rtos_delay_milliseconds`、`rtos_lock_mutex` 的本核中断关闭断言；断言输出缺少
调用者地址，不能确认是哪条业务或 SDK 路径。`test_bk_heartbeat_trace.py`
先因日志分析器不能识别新的断言定位字段失败，再验证其记录 API、调用者、
中断状态和 ISR 状态，并将事件判为失败。BK SDK 诊断补丁只在这两处断言
前用非阻塞日志输出调用者地址；须用同一固件实板复现并用 AP ELF 符号化，
再决定修复点。

2026-10-15 BK7258 心跳任务栈隔离：对照 Beken `bk_avdk_smp` 官方
`release/v3.1.1.8` 和 `release/v3.1.1.10` 源码，SMP 通用建栈接口在
`CONFIG_TASK_STACK_IN_PSRAM=y` 时会把心跳栈放进 PSRAM；官方另提供
`rtos_create_sram_thread()`，启动任务等关键任务使用它。新版心跳实现没有
自动将栈转到 SRAM。`test_bk_heartbeat_sram.py` 编译并调用实际 SDK 的公开
`mb_ipc_heartbeat_init()`，先因创建通用任务失败，再验证以原优先级创建
至少 1024 字节的 SRAM 栈。板级补丁只切换心跳建栈接口，BK release
双核编译与完整主机检查通过；该改动隔离心跳对 PSRAM 任务栈的依赖，
尚不能证明 AP 全局调度停顿已消除。实板须用新镜像覆盖此前空闲约 5、
8、27、56 分钟发生的超时窗口，并复验 H5/AI/房间退出。

2026-10-15 新镜像实板启动回归：`Oct 15 2026 14:56:35` 的 AP ELF 在
`0x02187496` 固定 HardFault，`CFSR=0x82`，读空地址。精确 ELF 符号化表明
PC 在 `psram_prvInsertBlockIntoFreeList()`，调用路径为
`bk_ipc_core_init()` → `rtos_create_thread()` → 首次 2048 字节 PSRAM 栈
分配。该时刻 `system_main.c` 尚未调用 `mb_ipc_heartbeat_init()`，因此不能
把启动崩溃直接归因于心跳任务本身。Beken SDK 的 `rtos_create_sram_thread()`
适合启动期 IPC 工作线程；板级补丁将此线程的 TCB 和栈移到内部 SRAM，避免
IPC 初始化依赖尚未稳定的 PSRAM 堆。`test_bk_ipc_boot_stack.py` 在旧 ELF
检查到通用建线程接口而失败，改动后检查构建产物的实际调用。PSRAM 堆空闲
链表为何在该次启动变成空指针仍需实板继续验证；新固件要先确认启动日志，
再做长时间心跳及 H5/AI 测试。

2026-10-15 后续实板日志：AP 心跳发送与 CP 接收均持续成功，随后 AP 停止发送，
CP 约 8 秒后报 `IPC[1]heartbeat timeout`。多次超时的 SYS 快照相同：
`run=0x00007060 cpu1=0x02150031 cpu2=0x02150239`；运行状态中的双核
halt 和断电位均未置位，但不能据此证明 AP 仍在执行指令。其中一次明确停在
`metrics-log-enter`，未出现 `METRICS` 输出。SDK 普通日志在缓冲区满时反复
等待信号量；周期指标仍直接使用普通 `BK_LOGI`。`test_bk_rssi_cache.py`
通过生产 `xiaotai_metrics_start()` 触发采样，修改前因调用阻塞日志失败，
修改后验证指标使用非阻塞日志；该测试已由 `tools/check.sh` 注册。日志缓冲区满时
可丢弃单次指标。其他超时没有共同的指标停点，仍需新镜像空闲长稳和 H5/AI
实板复验。另有多次启动记录 `reset=17`，SDK 定义为 HardFault，须单独收集
异常寄存器和对应 ELF 的调用栈。

固件版本规则见[固件版本与发布](../CONTRIBUTING.md#固件版本与发布)。`test_firmware_versions.py`
验证各板产品版本和构建调度，`test_boot_identity.py` 验证 BK 构建身份与最终
烧录内容；具体产物核对和发布完成条件统一按该节执行。

2026-10-14 串口 EP 覆盖：`test_serial_ep_override.py` 已注册到 `tools/check.sh`。
回归测试编译两端公开持久化接口和 URL 校验函数，覆盖设置、读取、恢复默认、
`http://ep-open.tangeopen.com/pre`、HTTPS、非法协议、空主机、用户信息、路径保留和非法端口，
并检查串口命令与启动时覆盖项接入。build.119（P4）、build.39（BK）、
build.52（S3）修正后该值只作为服务发现基址，自动追加
`/services`；TiRTC 地址由发现响应的 `tirtc-srv` 提供。回归测试先复现旧版将
保存值直传 TiRTC 的错误，再验证 P4/S3、BK 的启动接线。
BK 恢复出厂测试同步检查 EP 键被清除。P4 build.116、BK build.36 的串口设置、
重启持久化、恢复默认和实际连接仍需在对应实板验证。

2026-10-14 串口 EP 路径修复：build.116 与 build.36 的地址校验误拒绝 `/pre`。
`test_serial_ep_override.py` 先在两个公开持久化接口复现失败，再验证含路径地址
原样保存和读取，并检查 `ep-show`、`ep-set`、`ep-reset` 的命令注册；查询参数、
片段及空白继续拒绝。最终构建号为 P4 build.118、BK build.38；实际连接仍需
刷入新版固件验证。
立创 S3 build.51 使用同一 ESP-IDF 串口、持久化和启动覆盖实现，编译已通过，
实板串口设置与连接仍待验证。

2026-10-14 快捷呼叫与 AI 呼叫媒体选择：`test_default_video_call.py` 已注册
`tools/check.sh`。测试先因共享 P4/S3 运行时缺少能力选择接口失败，随后验证
P4 微信 VoIP 默认视频、设备联系人默认语音、S3 音频回退、显式语音请求，以及首页、实体键、AI 和联系人刷新后
的快捷呼叫都使用同一选择策略。BK 的音频信令保持音频。实板仍须验证 P4
视频上行与下行、S3/BK 语音呼叫及 AI 挂断后的媒体交接。
首页已有微信联系人时保持首页等待通话页，缺少联系人时才显示二维码；该页面切换
也由同一测试覆盖。

2026-10-14 BK AI 表情与点阵嘴：`test_bk_ai_face_animation.py` 已注册
`tools/check.sh`。测试先因缺少 Tag 优先策略和 PCM 点阵接口失败，修复后通过
生产策略与采样函数，并验证静音、播放停止及采样超时清空点阵。板级显示测试验证
队列提交不等待 DMA、局部区域自有缓冲、旧页面拒绝更新、整屏窗口恢复及 DMA 等待
失败时不提前释放缓冲。另验证整屏入队失败时不会短暂改变页面代次，避免显示线程
误丢旧页面的嘴巴更新。BK release 工程已编译；实板仍须检查闭眼与 Tag
切换、语音和唱歌时点阵同步、局部刷新花屏及音频连续性。

2026-10-13 P4 设备互呼 MJPEG 旋转裁切修复：`test_p4_mjpeg_fit.py` 已注册
`tools/check.sh`。测试先因缺少会话级显示策略接口失败；修复后通过真实转换器的
裁切与布局函数验证设备互呼四向 `contain`、微信 `cover`，并验证预热转换器在
连续切换会话时重设策略。主机测试不能证明液晶实屏效果，烧录后仍需检查四向画面
与微信通话。

2026-10-13 build.112 修复 UYVY JPEG 试验的实板 DMA2D 超时：build.111
的 `JPEG_ENCODE_IN_FORMAT_YUV422` 搭配 `JPEG_DOWN_SAMPLING_YUV420`
虽通过 IDF 5.5.5 参数验证，但该驱动 `enc_hb_tbl` 对此组合给出 0，
连续出现 `ESP_ERR_TIMEOUT`，上行无法发送。`test_p4_packed_mjpeg_uyvy.py`
先在实际设备呼叫 JPEG 配置上失败，再验证输入与输出子采样均为 YUV422；
build.112 双机日志已确认持续编码发送和下行解码，没有再出现 DMA2D 超时，
音频接收缓冲溢出为零；画面颜色、液晶实际刷新和长时间稳定性仍待验收。

2026-10-13 build.111 设备互呼 MJPEG UYVY 实验：新增
`test_p4_packed_mjpeg_uyvy.py` 并注册 `tools/check.sh`。测试先在公开转换
接口与生产 JPEG 输入格式上失败，修改后验证 1280×960 packed
O_UYY_E_VYY 半缩为 640×480 UYVY 的色度共享、亮度 2×2 均值、
缓冲边界、非法尺寸、DMA 输入缓存同步错误，以及设备互呼选择 YUV422
输入。build.111 曾选择 YUV420 JPEG 子采样，实板超时；见 build.112 记录。
微信 VoIP 路径未改。主机测试和编译不能证明
实板颜色、编码器接受输入或转换时间；当前实板结果见上面的 build.112 记录。

2026-10-13 build.110 设备互呼 MJPEG 8 fps 节拍对照：
`test_p4_fixed_call_bitrate.py`、`test_p4_call_mjpeg.py`、
`test_p4_video_quality.py` 和工程 `test_full_frame_uplink.py` 先通过生产
配置、采集策略和拥塞切档入口复现旧 10 fps 配置失败，再验证设备互呼
VGA/8 fps/4 Mbit/s 预算、125 ms 节拍，微信 VoIP QVGA/15 fps/200 kbit/s
保持不变。现有测试均已注册 `tools/check.sh`。build.109 实测上行约 7.5 fps，
所以本次设定只是对齐节拍；实际帧率、双端接收呈现和音频仍需实板验证。

2026-10-13 设备互呼 MJPEG 上行转换对照：build.108 实板在 4 Mbit/s
预算下 `budget-drop=0`，但 1280×960→640×480 PPA 颜色转换平均
213–256 ms，整条同步链只有 2.1–2.5 fps。`test_p4_packed_mjpeg_rgb565.py`
先在公开打包 YUV420→RGB565 接口验证旧实现因必须有 PPA 而失败，
修改后逐像素验证 2×2 全幅缩小、BT.601 有限范围颜色、RGB565 字节序、
缓冲边界、相机 DMA 缓存同步失败和非目标尺寸回退。已注册 `tools/check.sh`。
该 CPU 路径是否快于双向 JPEG 与 PPA 争用，仍需下一版实板阶段耗时验证。

2026-10-13 build.108 设备互呼 MJPEG 预算对照：build.107 实板显示每帧约
44–50 KiB，1.5 Mbit/s 预算只发送 3.7–4.2 fps，所有上行丢帧均为
`budget-drop`。`test_p4_call_mjpeg.py` 先在生产预算接口验证 4 Mbit/s 失败，
修改后验证令牌补充、整帧拒绝、CALL/VoIP 配置隔离；既有公开媒体配置
测试同步验证设备互呼 4 Mbit/s、微信 VoIP 200 kbit/s。
新增采集、转换、JPEG 编码和发送阶段耗时日志，用于实板判断 10 fps 的
本地处理上限；此版仍需硬件回归。

2026-10-13 build.107 设备互呼 MJPEG 黑屏：实板 `rx` 增长但 `decoded=0`，
追到媒体所有者在设备互呼状态重新启动 H.264 渲染器。
`test_p4_call_mjpeg.py` 先在生产模式选择入口失败，修复后验证 CALL/VoIP
选择 MJPEG、H5 原选择不变，并核对设备互呼资源入口；
`test_video_ingress.py` 先验证旧实现遗漏渲染提交失败计数，再验证错误计数。
测试已由 `tools/check.sh` 执行；同时硬件编解码、两台设备互呼画质仍需实板验证。

2026-10-13 build.105 QVGA 下行画布写入优化：转换后只清理画面外的黑边，
不再在复制完整中心区域前先清零该区域。`test_p4_direct_i420.py` 通过公开
转换入口先验证旧实现没有调用边缘清理而失败，修改后验证脏缓冲仍留黑边、
中心像素正确、复用和回退行为不变；已在 `tools/check.sh` 注册。主机测试
和编译不能证明实板双向通话更快，须比较 `QVGA phases` 和两端帧率。

2026-10-13 build.106 设备互呼 MJPEG 对照：`test_p4_fixed_call_bitrate.py`
通过生产媒体配置入口验证设备呼叫为 VGA/10 fps/1.5 Mbit/s 预算，
微信 VoIP 仍为 H.264 QVGA/15 fps/200 kbit/s。`test_p4_call_mjpeg.py`
在生产预算函数上验证超预算整帧丢弃和恢复，并检查 CALL/VoIP
各自的媒体能力上报；`test_p4_video_submit_probe.py` 通过真实提交入口
验证 CALL JPEG 与 VoIP H.264 分流。以上测试已注册 `tools/check.sh`；
JPEG 硬件编解码和双机双向音视频仍需实板验证。

2026-10-13 build.104 QVGA 下行性能分段诊断：`test_p4_direct_i420.py`
先在公开转换入口断言颜色转换、画布清零、居中拷贝的计数和累计耗时，
旧实现失败；修改后通过，并继续验证像素布局和失败回退。测试已在
`tools/check.sh` 注册。实板需比较每 10 秒的 `QVGA phases` 与 `VRX`
日志，并采集对端 `CAM`，才能区分本机转换、PSRAM 争用和对端送帧不足。

2026-10-13 build.103 设备互呼 QVGA 下行批量颜色转换：
`test_p4_direct_i420.py` 经生产公开转换入口先因仍使用逐像素软件路径失败，
修改后验证 `esp_image_effects` 批量 I420→RGB565、原尺寸居中、黑边、
句柄复用、组件失败回退及不提交 PPA。现有 `test_call_video_fit.py` 保留
0°/90° 软件回退逐像素几何验证；两项均由 `tools/check.sh` 执行。
主机测试不能证明 P4 上的颜色转换耗时或 15 fps 呈现，须实板复测。

2026-10-13 build.102 设备互呼下行原尺寸居中：`test_p4_direct_i420.py`
通过生产转换接口先复现 QVGA 仍走 PPA 的旧行为，修改后验证 0°/90°
绕开 PPA；`test_call_video_fit.py` 通过生产几何和软件像素路径验证
320×240/旋转后 240×320 的居中位置、原像素和四周留黑。
两项均由 `tools/check.sh` 执行。颜色转换仍必需；软件转换耗时、
双机实际呈现帧率和音频连续性须实板复测。

2026-10-13 build.101 将微信 VoIP 上行也设为 320×240、15 fps、200 kbps，
与设备互呼一致，仍由 1280×960 全幅采集经 PPA 完整缩小。
`test_p4_fixed_call_bitrate.py`、`test_p4_video_quality.py`、
`test_full_frame_uplink.py` 的微信公开配置、切档和采集策略断言在旧实现
先失败，修改后验证两种通话同参数、67 ms 节拍、GOP30、固定码率，
H5 不变。`test_release_audio_and_ui.py` 保护显式配置。上述测试已由
`tools/check.sh` 注册；微信下行仍是既有 MJPEG 路径，实际双向帧率、
PPA 争用、画质和声音需实板验证。

2026-10-12 build.100 设备互呼 QVGA 对照：build.99 的原生 VGA 寄存器
试验仍有彩边，撤回 `0x3820=0x41`。设备互呼改为已有 1280×960 全幅
采集，经 PPA 1/4 倍完整缩到 320×240，设备互呼为 15 fps/200 kbps；微信 VoIP
保持全幅采集后输出 VGA，H5 参数不变。`test_p4_fixed_call_bitrate.py`、
`test_full_frame_uplink.py`、`test_p4_native_vga.py`、`test_p4_video_quality.py`
先在旧实现失败，再验证设备/微信模式隔离、67 ms 采集间隔、GOP30、
固定码率和会话切换；
`test_p4_uplink_half_scale.py` 进一步检查 QVGA 走 PPA 而非 CPU box。
均已在 `tools/check.sh` 注册。真实 PPA 延迟、画质、双方呈现帧率和音频
连续性须实板验证。

2026-10-12 build.99 原生 VGA 对照：设备互呼与微信 VoIP 恢复 640×480
传感器采集，保持 8 fps/256 kbps，单独试验 OV5647 `0x3820=0x41`。
`test_p4_native_vga.py` 先在实际格式协商出口断言该寄存器失败，
`test_full_frame_uplink.py` 先在设备/微信采集尺寸上失败；修改后验证原生
VGA 不预热缩放器，H5 仍采集 1280×960。两项已在 `tools/check.sh`。
这些测试不能判断彩边是否改善，须用实板同场景原始帧及双向通话复测。

2026-10-12 build.96 增加仅在设备空闲时使用的 `camera-snapshot` 本地抓帧命令。
它通过生产 camera driver 获取一帧，在当前全幅采集档使用生产缩放器生成
640×480，在原生 VGA 档直接保留该帧；随后释放相机再导出校验过的字节。
`test_p4_camera_snapshot.py` 对缺少真实抓帧入口先失败，再用驱动/缩放接口桩
验证两种采集路径、忙时拒绝和帧资源归还；已注册统一检查。后续增加
`capture-native`/`capture-full` 显式选择，支持同一固件内对照。build.98
发现第 0 帧曝光未稳定，回归测试先因只抓一帧失败；现在丢弃间隔 250 ms 的
四帧，约一秒后再保留第五帧，并验证每帧归还。源图像只保存在本机临时文件，
不加入仓库；实板图像对照记录在媒体诊断文档。

2026-10-12 build.95 修正 build.94 实板启动失败：IDF 5.5.5 的
`esp_cache_msync()` 拒绝 M2C 与 UNALIGNED 标志同时使用，缩放器预热因此
返回 `ESP_ERR_INVALID_ARG` 并使媒体初始化中止。`test_p4_uplink_half_scale.py`
先在生产缩放入口的缓存调用标志上失败，再验证输入使用合法 M2C 标志、
输出仍正确同步，以及完整 1280×960 像素结果。真实 DMA 采集缓冲按 64 字节
对齐分配；实板启动和通话画面仍需重新验证。

2026-10-12 build.94 缩放竞争对照：完整 1280×960→640×480 的无旋转 2 倍
缩小改用 CPU box 平均，绕开上行 PPA；采集/编码沿用 build.93。
`test_p4_uplink_half_scale.py` 在旧公开缩放入口返回 PPA 故障时先失败，
修改后逐像素核对 Y/U/V、完整尺寸、缓冲区边界、
DMA 输入/输出缓存交接和错误、旋转/裁剪/其他倍率仍走 PPA；以 ASan/UBSan
运行并注册 `tools/check.sh`。CPU 时间、音频溢出与双向帧率仍需实板验证。

2026-10-12 build.93 画质对照：设备/微信视频改为 1280×960 原模式采集，
完整缩小至 640×480；编码仍为 8 fps、256 kbps、GOP16。
`test_full_frame_uplink.py` 在 build.92 原生 VGA 选择上先失败，再通过公开
相机策略验证正常/拥塞状态均选 1280×960，并用生产缩放几何验证完整
1280×960 输入、零偏移、1/2 缩放及 H5 原参数。入口已注册 `tools/check.sh`。
VoIP 彩纹、视野、曝光及缩放对音频溢出和视频帧率的影响仍需实板对照。

2026-10-12 build.92 低负载对照：设备/微信视频固定为 640×480、8 fps、256 kbps，
GOP16。`test_p4_fixed_call_bitrate.py` 先在旧 12 fps/512 kbps 配置上失败，
再验证实际配置、125 ms 采集间隔、GOP16、固定传输范围以及恢复/新会话仍为
8 fps/256 kbps；`test_p4_video_quality.py` 保留拥塞停发、恢复和会话边界验证，
`test_full_frame_uplink.py` 验证原生 VGA 无裁剪与场景切换。入口已注册
`tools/check.sh`。音频溢出增量、双向视频帧率和转换耗时需两端实板对照。

2026-10-12 P4 两档试验回归：`test_p4_fixed_call_bitrate.py` 执行生产配置、
规范化、采集间隔与 GOP 入口，先在旧配置失败，再验证高档 VGA/12 fps/512 kbps、
低档 VGA/8 fps/256 kbps、84/125 ms 和 GOP24/16。`test_p4_video_quality.py`
先在低档持续拥塞未停发的断言失败，再覆盖持续积压/新发送失败停发、健康恢复、
停发锁定及新会话复位。工程 `test_p4_video.py` 覆盖停采集、不再提交、
下行所有权保留、陈旧操作拒绝及手动关闭/开启重试。以上入口已注册 `tools/check.sh`。
`test_p4_sdk_link.py` 先因 5.5.5 被旧门禁拒绝失败，再验证实际组件接受 5.5.5、
仍拒绝旧版本，保持原 SDK 哈希/架构/ABI 约束。声学、实际双向帧率及停发后的声音
与远端画面需要实板验证，步骤见[两档试验](../docs/development/MEDIA_PIPELINE_DIAGNOSTICS.md#两档通话试验2026-10-12)。

以下 build.83～87 段落保留历史回归与实测背景，参数不代表本轮试验目标。

`test_p4_fixed_call_bitrate.py` 回放生产设备通话配置、规范化和码率范围入口，
确认 build.83 对照版从 150 kbps 起步并固定，保持 VGA/5 fps；H5 和微信的
自适应范围仍有效。`test_video_congestion.py` 验证固定范围不提交给要求严格
上下界的 SDK API，积压和 SDK 升码率不会重配编码器，后续自适应会话恢复
原控制器。两项都接入 `tools/check.sh`。双机正常视频通话仍待实板验证。

`test_p4_sdk_link.py` 配置实际 P4 SDK CMake 入口，验证链接的是
`third_party/tirtc/lib/libTiRTC.a`、库内版本为 2.5.0 且对象为 RISC-V。
build.84 对照版进一步核对 fixes2 库哈希与构建契约；build.83 回退点为
`c4eedec`，其原始 BIN/ELF 保留在工程 `artifacts/1.0.0+build.83/`。
迁移只调整厂商文件位置；`components/tirtc_sdk/` 保留工程校验和链接配置。

`test_s3_sdk_link.py` 对两个 S3 工程执行同样的实际 CMake 链接检查，
要求使用工程内的 2.5.0 Xtensa 库；两类芯片的库独立保存。升级 SDK 后仍需
重新编译并在设备上验证音视频通话、AEC、挂断重拨及重连。

`test_p4_tirtc_stack.py` 通过真实链接包装函数回放 SDK 的线程创建调用：
P4 2.5.0 的 `rtc_thread` 从 8 KiB 提升至最少 24 KiB，更大请求、其他线程、
入口函数及返回值保持原样。build.76 实板日志在约 8 KiB 栈的 `sprintf`
路径触发栈保护；该测试保护创建边界，不模拟闭源 SDK 的完整栈峰值。
修复版仍需复测双向视频、码率反馈/关键帧请求及长通话，并确认启动日志
出现 `TiRTC task stack adjusted: name=rtc_thread requested=8192 effective=24576`。


开发阶段的可复用音视频统计及双机采集步骤见
[`MEDIA_PIPELINE_DIAGNOSTICS.md`](../docs/development/MEDIA_PIPELINE_DIAGNOSTICS.md)。
`test_pipeline_diag.py` 覆盖生产 SDK 发送入口、实际 socket 包装、并发与开关/复位；
SDK 正数成功返回与 sendmsg 分散发送也纳入回归。
默认关闭，编译关闭路径也纳入回归。实板链路耗时与诊断开销仍需两端日志验证。

`test_p4_hosted_rx_transfer.py` 回放真实 SDIO 接收循环，覆盖 16 位 HAL
边界、计数回绕及第一/中间/最后一段读取失败。只有成功读取的字节计入接收计数；
失败批次不交给解析器，释放总线锁并归还缓冲后走既有主机重启恢复路径。
HAL 失败时无法提供实际消耗长度，不能安全地继续解析或简单重试。这是底层读取
错误的恢复措施，不用于视频拥塞，也不证明此前 VoIP 网络失联的原因已排除。
实板仍需验证读取故障后的重启、联网和业务恢复。

`test_call_error_reset.py` 回放真实呼出/接听准入函数，验证微信和设备通话
成功进入新会话时清除历史错误码，准入失败保留原错误。两项检查均接入
`tools/check.sh`；保留 build.69 冻结的音视频参数。

### P4 通话收发链路诊断（build.71）

后续 build.72 的原生 VGA 采集实验、参考依据和验收步骤见
[`P4_OV5647_NATIVE_VGA.md`](../docs/hardware/P4_OV5647_NATIVE_VGA.md)。
`test_p4_native_vga.py` 回放实际传感器/V4L2 格式协商和无缩放预热入口；
`test_full_frame_uplink.py` 验证两类对讲选择 640×480 原生采集，H5 仍选
1280×960。发送帧率、码率和音频参数保持 build.69 基线；实板出图与性能仍待验收。

本轮只增加被动观测，不修改 build.69 冻结的音视频参数、队列容量和任务优先级。
P4 的 `CONFIG_ESP_HOSTED_PKT_STATS=y` 每 10 秒输出一行 `[DEBUG-link71] host`：

- `rx-valid`：主机 SDIO 解析到的有效 STA 包；不代表 C6 无线侧全部收包。
- `stack-ok/stack-fail`：主机向网络接收回调交付的成功/失败次数；成功不等于
  TiRTC 或应用已收到。分配失败及尚在排队的包不会进入这两个计数。
- `tx-queued/drop/bus-written`：STA 包成功进入 SDIO 发送队列、发送前被丢弃、
  成功完成总线写入的次数；总线写入不代表 C6 发射成功或对端收到。
- `throttle`：从机要求主机暂停 Wi-Fi 发送的标志。

这些是整个联网周期内全部 STA 流量的累计计数，包含 MQTT/HTTP，字段为独立原子
快照。与同一设备的 `CAM`、`p4_video`、`VRX` 和 `status` 对照；不可直接把它们
当作本通话的帧数或成功送达证明。`test_p4_link_diagnostics.py` 回放真实网络回调
及统计输出，`test_p4_hosted_tx.py` 覆盖入队失败计数与所有权，均接入统一检查。

实板采集步骤：

1. 两端记录板型、`status` 固件版本和 ELF 前缀，分别保存串口日志；日志不要公开
   原始凭据、令牌或联系人身份。P4 使用同一诊断版本；其他板保持其现有固件。
2. 同一网络和同一对端，先做 60 秒纯语音，再做 60 秒设备视频对讲。
   测试者轮流持续说话并移动画面，标记卡顿的实际时间及方向；静默时不来音频
   不能据此判故障。各设备 uptime 不同步，不能直接按毫秒时间戳跨设备配对。
3. 卡顿后两端各执行一次 `status`，挂断后再各执行一次，保留整个通话和前后
   至少 10 秒日志。不要在此轮同时调整 Wi-Fi、码率或帧率。
4. 用现有低频日志生成两端观测报告：

   ```bash
   python3 tools/analyze_call_link.py local.log --peer-log peer.log \
     --out tests/.local/reports/call-link.json
   ```

工具按本机媒体会话分段，列出主机计数增量、SDK 视频收发速率、发送队列变化、
相机/渲染输入速率和音频溢出；处理重启和计数重置，不输出原始日志内容。
它不自动认定两端会话匹配，不把缺失统计当作零，不自动归责设备或平台。
`test_call_link_analyzer.py` 覆盖当前日志、会话隔离、重启、计数重置和数值白名单。
定位完成后关闭该临时统计并移除 `[DEBUG-link71]` 诊断输出。

修改或新增功能时，先写一个能够复现目标行为或故障的测试，并确认它在修改前失败；
再完成最小实现，让测试通过；最后在全部检查通过的前提下整理代码。测试应优先调用
`product/include/` 下的公开业务接口。只有驱动适配、并发退出或平台 SDK（软件开发
工具包）无法通过公开接口观察时，才增加平台内部契约测试。

测试按职责分层：

- 函数级测试验证编解码、边界值、超时回绕和非法输入；
- 功能级测试验证音频、AEC（声学回声消除）、相机、触摸、显示和资源生命周期；
- 业务接口测试验证 H5、AI、设备呼叫、微信 VoIP、多人对讲之间的准入和交接；
- HIL（硬件在环测试）验证 DMA（直接内存访问）、GPIO（通用输入输出）、真实音频方向和长时间稳定性。

业务规则必须至少包含正常路径、拒绝路径、超时路径、重复事件和陈旧回调。以呼入为例，
测试应证明通知只进入等待态，不会打断当前业务；拒接或超时后原业务继续；用户明确
接听后才释放当前媒体；已有来电时第二个来电返回忙；媒体占用时 H5 实时查看被拒绝。
这些用例应接入 `tools/check.sh`，不能依赖开发者记住单独执行。

新增板卡后，必须把该板已有的测试入口接入 `tools/run_host_tests.sh`。测试文件仅仅
存在于板卡目录不算覆盖，统一入口未执行的测试视为无效。

自动化测试、诊断和能力检查生成的报告统一写入 `tests/.local/reports/`。该目录只保存
本机结果，不提交 Git；需要作为发布证据的结论应在确认后写入对应板卡的验证状态。

GitHub Actions 在每次 `push`、每个 Pull Request 和手动触发时执行同一个
`tools/check.sh`，且没有路径过滤；因此文档、产品核心、公共平台或任一板卡目录的
改动都会运行全量主机测试。分支保护应把 `Host tests / test` 设为必需检查，才能
真正阻止失败变更合入。

`test_h5_talkback.py` 执行生产 H5 建连分支与 SDK 订阅边界，验证播放就绪后
订阅下行音频 14、订阅失败退出，以及不会为其他模式或空连接订阅 H5 音频。
该检查不替代浏览器麦克风授权、真实网络传输和扬声器出声验证。
`test_video_stream_routing.py` 对生产发送、订阅、退订和关键帧请求接口执行
100 轮 H5/设备呼叫/微信视频回放，按共享媒体合同核对上下行流号，
并验证错误流号和旧连接的退订不会关闭当前视频。

## 绑定与启动故障回归

统一检查还执行 `tools/tests/test_onboarding_faults.py`，从生产代码编译主机夹具，
在 SDK 边界注入时钟初始化失败、首次同步超时、重复授权消息和 NVS 保存失败。
它检查重试复用 SNTP、等待 ACK 时不覆盖授权、保存失败不改变活动身份，以及绑定门禁
只在平台启动成功后解除。`test_binding_completion.py` 检查绑定 ACK 后及时取消播报。

`test_status_metric_validity.py` 检查未采集的性能数据明确显示不可用，避免把零值当作
真实耗时；能力上报测试解析 P4、S3、BK 的 JSON，检查 H5 上行 10/11、下行 14/15。
产品核心的覆盖率不代表这些 SDK 适配路径的整体覆盖率。主机测试能提前发现状态与
失败恢复问题，但真实跨核调度、Flash/cache、DMA 和网络时序仍需要实板回归。

`test_playback_waveform.py` 验证 ESP S3/P4 首页的 AI 下行嘴巴动效：P4 从成功写入的
PCM 提取有符号峰值绘制波形；S3 使用与 BK 相同的七列幅度点阵和门限，按高度固定
显示青、绿、暖黄三档颜色。无有效 AI TAG 时固定眯眼，后续 PCM/麦克风活动变化
不重绘眼睛。表情预览不继承首页动画。扬声器静音、切换媒体 owner/代次或 150 ms
无新播放后停止动效；S3 回到静态七点，P4 恢复普通嘴巴。有效 TAG 展示完整对应表情。
测试覆盖 BK/S3 幅度一致性、锁竞争丢帧、过期/代次拒绝、点阵颜色、嘴巴以外像素不变、
相同幅度不重画和 TAG 切换。`test_s3_opus_media.py` 与 `test_p4_ai_playback.py`
同时验证生产播放路径。实板仍须验证音乐/语音同步、静音/停止恢复、表情切换无残影
及持续播放时 UI 刷新耗时；主机绘图夹具不能证明屏幕实际流畅度。

`test_s3_touch_targets.py` 执行生产按钮构造和房间键盘布局，验证 S3 首页 48×48
快捷入口的边缘命中、返回键与设置首行不重叠、十二个加宽数字/操作键的触摸边界，
以及首页透明点击层不遮挡快捷入口；已注册在 `tools/check.sh`。
`test_room_keypad.py` 继续覆盖数字输入、删除、密码掩码和创建/加入房间业务流程。
实际手指操作、屏幕边缘触摸精度仍需 S3 实板验收。

波形只清除并重画嘴巴，眼睛仅在几何/颜色变化时重画；合并 LVGL Canvas 的自动整图
失效请求，嘴巴更新按图像缩放后的局部区域刷新。相同波形不重画。动画最多 10 FPS，
单次表情绘制达到 10 ms 时波形暂降至 5 FPS，3 秒未再次超时后恢复；表情和停止
播放不受波形限速延迟。`test_playback_waveform.py` 同时验证局部绘制、缩放区域、
外层失效开关恢复、跳帧与计时回绕；该入口已注册在 `tools/check.sh`。
P4 `status` 现在采集解码、输出锁等待、PCM 写入耗时的会话最大值与平均值，
`test_p4_ai_playback.py` 在真实播放入口注入解码/锁/写入延迟并验证公开快照。
这些耗时包含被调度挂起的时间；正常 I2S 写入等待不等于卡顿，仍需与实听和动画开关对照。
P4 `.60` 保持 `.59` 的缓冲策略，将成功播放的首帧/每 100 帧汇总降至 DEBUG，
默认 INFO 构建不再在播放线程持有输出锁期间执行这些串口打印。错误日志、计数和
`status` 诊断保留。`test_p4_ai_playback.py` 的真实播放入口用 INFO 日志探针验证
首帧和第 100 帧均无默认级别成功日志；原有失败分支和 PCM 时序测试继续覆盖。
这是减少可避免的日志开销，未证明它是先前卡顿的根因，也未替代实板听感对照。

P4 `.59` 试验 AI 播放预缓冲：按 AI 20 ms 包合同累计 10 包起播（目标 200 ms），
估计 DMA 播放尾部耗尽后累计 12 包恢复（240 ms），连续 10 秒未再耗尽恢复 200 ms。
每次等待上限 240 ms，单包短回复不会无限等待；缓冲已有足够包时立即播放。
只在播放线程等待，不持有音频输出锁、不阻塞收包线程；会话切换在 5 ms 轮询间隔后取消
等待并归还当前槽。其他通话不预缓冲，保留原来的队列容量和延迟策略。
`test_p4_ai_playback.py` 驱动生产播放线程及预缓冲入口，验证完整包顺序、初始预填充、
DMA 仍有声音时不误判、恢复预算与稳定回退、单包超时放行、陈旧代次取消和非 AI 旁路。
`status` 的 `prefill-waits` 记录实际等待次数；`Audio AI buffer` 显示目标及恢复次数。
恢复次数依据软件 DMA 时间估计，包括正常回复间隔，不能作为硬件欠载计数。
实板需比较句中断续次数和回复延迟，并检查短回复、连续多轮、打断/停止、静音和 TAG 表情。
该策略吸收短时到包波动，不能补回长期断流或已经溢出的音频；未完成听感验收。

P4 `.58` 增加只读音频间隔观测（发布日志统一为 `Audio gap observation:`）：相邻成功 PCM 写入完成间隔至少
60 ms，或同帧解码/锁/写入累计至少 40 ms 时，保存最近一帧的启动后时间、帧号、
该包到达间隔、排队时间、待播数量及三段耗时；在 `status` 中打印，不在音频线程追加日志，
不改变缓冲、预填充或播放策略。原子序号拒绝读取中的混合帧快照；新会话清空观测。
正常回复边界、启动、预缓冲等待和长音频帧也可能触发，不能据此直接认定卡顿。
到达间隔仅观察 SDK 回调，不能区分网络、平台发送或 SDK 内部调度。
实听句中断续后立即执行 `status`，保留前后串口日志及大致发生时间；
该条记录只有一帧，后续正常回复边界可能覆盖它。主机延迟注入验证的是观测准确性，
没有复现或证明修复实板的偶发听感问题；该诊断仅在主动执行 `status` 时输出。


ESP 串口 `face-animation off` 同时跳过新增的 PCM 波形采样、波形绘制，
下一次 UI 刷新时 S3 恢复静态点阵、P4 恢复普通表情；`face-animation on` 恢复，重启默认开启，不保存设置。
`test_playback_waveform.py` 在统一检查中验证公开控制接口、命令参数与采样旁路。
排查卡顿时，在同一次持续播放会话中按 on/off/on 各观察 30 秒，切换 2 秒后记录
`status` 起点和终点，比较 `rx-overflow` 增量、`pending` 和实际卡顿；
`queue-peak` 是会话历史峰值，不能用关闭后的历史峰值判断动画仍然有影响。
这个对照只能定位新增动画的影响，不能代替网络收包间隔和解码/I2S 写入耗时测量。
同一回归还验证 AI 会话的有效表情 TAG 优先于本机选择和动画。`product/tests/test_ai_view.c`
经统一入口验证当前回复的 TAG 来源、中文别名、新轮次/新回复清除旧 TAG、同组字幕
追加保留 TAG；新回复无有效 TAG 时恢复默认，不让上一轮 TAG 残留。

## 唤醒录音边界

`test_wake_audio_boundary.py` 通过实际预录队列 API 注入检测窗口、推理期间和
联网期间三段不同 PCM，先复现旧实现回带检测前 800 ms，再验证只保留检测帧
结束后的音频。覆盖空缓存仍可绑定会话、后续采集、过期检测与未到达的时间边界；
入口已登记 `tools/check.sh`。原有语音可靠性测试继续覆盖背压、代次、超时、溢出、
断帧和部分包衔接，S3 Opus 测试覆盖补发转实时音频的生产入口。

`test_wake_ack_overlap.py` 补充执行 S3/P4 实际媒体 API、共享 PCM 队列与 runtime
媒体准入入口。旧实现先在“回应期间缓存必须为空”失败；修复后覆盖快速服务端确认、
播放及尾音整帧丢弃、连接未完成时缓存后续语音、取消/旧 token、重复播放、无 worker
和播放卡住的有界失败。`test_voice_reliability.py` 执行实际提示音 worker，验证静音或
输出拒绝时也完成门禁交接，取消会清除待准入状态。入口均登记 `tools/check.sh`。

S3/P4 保留“在的”并与网络建连并行；播放返回后隔离 160 ms，再接收完整新采集帧。
这是有声回应后开始听的交互，重叠语音有意丢弃。主机测试证明软件时序，不证明声学
回声已消失：仍须实板覆盖快/慢网络、男女声、最大音量、静音、重复唤醒/取消，以及
紧接回应说话的首字完整性；根据 DMA 和房间混响证据判断尾音窗口是否足够。

## 产品业务验收矩阵

P4 的 H5、微信 VoIP、设备视频通话分别使用独立的上行编码参数：H5 为
1280×960 / 20 fps / 3 Mbit/s，微信 VoIP 为 640×480 / 8 fps / 500 kbit/s，
设备通话为 640×480 / 5 fps / 600 kbit/s。码率是编码目标，不是网络流量上限；
微信及设备通话仍保留拥塞暂停视频、保留语音的保护。下行显示尺寸和旋转合同不变。
`test_full_frame_uplink.py` 已注册在 `tools/check.sh`，执行生产参数构建、配置切换和
相机策略接口，验证完整视野及 100 轮三场景切换不继承上一场景的降级参数。
实板仍需分别验证三场景的实际帧率、码流、双向声音、视频暂停/恢复及切换后的参数。

P4 `.69` 将 starter 音频任务接入板级优先级策略：采集 18、播放和发送 17，
高于持续 SDIO 数据转发的 16；SDIO 中断读取/批次交接仍为 23/22，视频任务保持原值。
此前创建入口直接使用 7/8，未采用板级音频优先级。`test_p4_audio_scheduling.py`
已注册在 `tools/check.sh`，执行生产任务创建片段，验证音频相对数据转发的调度顺序、
PSRAM 栈属性和创建失败返回。测试在旧优先级失败、修改后通过。
这修正了音频低于持续数据转发的优先级配置，尚未证明是实板 VoIP 音视频卡顿的唯一原因；
需用同一网络、同一对端分别对照纯语音和视频通话，并验证 H5、AI、设备通话、
MQTT 连通性及长时间运行。到包长间隔还可能包含 SDK 或传输路径等待。

2026-10-10 用户确认暂缓性能优化，以 P4 `1.0.0+build.69`（ELF 前缀
`2fa4697a0`）作为当前参数基线，优先目标为功能可用、运行稳定、不崩溃。
保留上述三套独立视频参数及 `.69` 音频任务优先级；非 AI 音频保持
G.711 A-law / 8 kHz / 单声道 / 64 kbit/s，上行 20 ms（160 B），接收按实际包长解码。
AI 保持 Opus / 16 kHz / 单声道 / 上行目标 16 kbit/s VBR / 20 ms；
AI 接收 64 槽、预缓冲 200 ms、恢复目标 240 ms，其他对讲接收 8 槽且不预缓冲。
保留现有拥塞暂停视频和过时画面隐藏策略。未经用户重新要求，不继续调整码率、
帧率、分辨率、缓冲或调度优先级；功能故障和崩溃修复仍以该基线作回归对照。
用户反馈 `.69` 视频通话声音有所改善、纯语音顺畅；偶发断续仍存在，未记录为已修复。

| 功能 | 主机侧必须验证 | 上板必须验证 |
|---|---|---|
| H5 实时查看/对讲 | 媒体所有权；音频 10/14、视频 11/15；断开后资源释放 | H5 能听设备；设备能听 H5；视频连续；近距离回声留档 |
| AI 对讲 | 按板卡媒体合同校验编码与帧长；服务端确认启动后启用媒体；旧事件隔离 | 双向多轮对话；AEC 不自激；主动/远端结束均可再次启动 |
| 设备呼叫 | 联系人解析；音频/视频选择；取消和超时状态 | 发起、接听、拒绝、挂断；双方音视频方向正确 |
| 微信 VoIP | 音频 stream 0、视频 stream 1；来电排队；会话交接 | 呼入和呼出；微信到设备、设备到微信均有声音；重复通话 |

## 驱动测试

板级音频、摄像头、屏幕和触摸驱动应同时具备三类证据：

1. 接口契约测试：空指针、非法格式、帧所有权、启停顺序和停止时阻塞读写的退出；
2. 主机假实现测试：至少连续启停 100 次，并覆盖 8 kHz/16 kHz 切换；
3. HIL：真实 DMA/I2S/ADC/DAC/摄像头运行，记录首帧、吞吐、丢帧、栈余量和资源释放。

硬件测试失败时，不能只记录“无声音”或“花屏”。音频至少记录方向、stream ID、
codec、采样率、首帧计数、发送返回码和结束统计；视频至少记录分辨率、编码、首个
关键帧、实际帧率和丢帧数。

## 发布门禁

`test_release_identity.py` 通过发布 CLI 验证 M5 独立标签、标题、真实 build 编号和
损坏附件拒绝；在未登记 M5 的实现下失败，登记后通过，入口为 `tools/check.sh`。
同一回归验证立创 S3、微雪 P4、立创 BK 的标题包含完整板型，已有标签保持不变；
标题仍为芯片简称时先失败，更新映射后通过。合并镜像仍按 ZIP 清单逐段校验。

发布候选包按[固件版本与发布](../CONTRIBUTING.md#固件版本与发布)执行目标板卡的
完整检查、发布配置构建和打包。`--all --keep-going` 用于跨板批量构建检查，
发布单块板不要求编译所有板卡。

构建成功只证明编译组合成立。每块要发布的板卡仍需使用本次构建产物执行上述产品
业务矩阵；未完成 HIL 的包必须明确标记“待验证”，不能沿用旧版本结论。

`tools/check.sh` 还会用 AddressSanitizer 和 UndefinedBehaviorSanitizer 重新编译并
执行平台无关的 C 产品核心，用于在上板前发现主机可复现的泄漏、重复释放、释放后
使用、越界访问和未定义算术。DMA、中断及跨核 SDK 路径仍须执行板卡验收文档中的
HIL 用例。

## ESP 发布日志与边界回归

默认 INFO 保留启动、会话切换、用户操作及失败原因；周期性的上下行成功统计、
HTTP 回调细节降至 DEBUG。移除呼叫诊断中的原始响应消息、设备及房间标识，
`status` 仍可主动查询媒体计数、队列、耗时和最近的音频间隔观测。
`test_release_logging.py` 执行生产 TiRTC 收包回调，验证 1000 包完整交付、
成功路径无 INFO 打印，非法媒体仍记录警告；已注册到 `tools/check.sh`。
现有产品核心、呼叫和状态快照测试覆盖日志机械整理后的业务行为。

`test_p4_ai_playback.py` 另覆盖静音时不预缓冲、等待中静音取消，以及停止后立即
重开会话时旧槽归还不会破坏待播计数。槽计数覆盖全部尚未归还的池槽，新会话不
清零旧槽债务；音频间隔快照的会话清理与播放发布使用同一输出锁。
`product/tests/test_ai_view.c` 通过公开协议入口验证独立有效 TAG 在紧接的新字幕
到来时保留一次，后续无 TAG 新回复恢复默认；新轮次或结束清除待关联 TAG。
这些边界仍需实板验证静音/恢复、快速切换及 TAG 与字幕实际事件顺序。

## P4 视频拥塞与音频保护候选版本

P4 `.63` 在 SDK 发送边界按「当前占用 + 本帧长度」检查 H264 入队，预留容量为
已配置队列的 1/8、最多 64 KiB；2 MiB 配置下预留 64 KiB。关键帧同样遵守容量边界，
音频发送不受视频门禁限制。相机原有丢帧路径要求重新发送关键帧后恢复，避免继续发送
依赖已丢参考帧的 P 帧。容量未知时不推测 SDK 默认值。预留并非音频独立优先队列，
不能清除 SDK 已有积压，也不能保证断流期间音频一直可发送。

SDK 码率回调只保存当前连接、上行视频流和代次的最新建议。P4 媒体 owner 每 500 ms
合并 SDK 建议和本地队列压力，再调用现有 governor：占用达到 50% 时请求至少降低
当前码率的 25%；占用高于 10% 时禁止恢复升码率。实际变化遵守 governor 已有的
最低码率、降档冷却和缓慢恢复规则，不在 SDK 回调或 UI 任务重配编码器。
S3 共用发送适配与日志整理，但尚未接入 P4 的 H264 硬件码率控制；BK 不受本次改动影响。

`test_video_congestion.py` 驱动生产发送接口、SDK 回调和媒体 owner 策略，验证容量
边界（含小队列与关键帧）、音频旁路、正确流号、旧代次隔离、最新建议合并及相机关闭。
`test_release_logging.py` 验证连续 1000 次失败按 5 秒限频且全部计数、恢复只记录一次、
代次重置、时钟回绕，以及提示音被实时媒体/静音取消时不误报警。两项均由
`tools/check.sh` 执行。`test_p4_ai_playback.py` 另驱动生产启动入口，验证取消提示音后
先获得输出锁并启用输出，再发布媒体激活；锁超时不能留下激活状态。

实板仍须验证：弱网上行时微信视频清晰度/帧率降档与恢复、音频连续性、发送占用是否
下降、恢复后有可解码关键帧、铃声与首段通话声音交接、相机隐私开关、重复通话及
H5/设备呼叫/AI 回归。ICE 心跳超时参数未改，长时间无对端响应仍可能断开，不能以
主机保护策略测试通过代替网络路径和平台侧定位。

P4 `.64` 修正 SDK 码率范围设置失败后跳过本地保护的缺陷。SDK 反馈为可选能力，
每个连接只探测一次；返回不支持或其他错误时仍按发送占用降档，并允许缓慢恢复。
`test_video_congestion.py` 补充不支持、其他错误、连续轮询和新连接场景，旧实现因
未执行本地降档而失败，修复后通过。`test_p4_video.py` 补充发送失败计数与返回码
验证；旧实现未更新计数而失败。两项已在 `tools/check.sh` 注册。
每个连接记录一次 SDK 能力返回码，每 5 秒的视频摘要增加发送成功/失败、最近失败码
及缓冲占用/容量，以区分本地门禁和 SDK 发送失败。挂断后的发送计数清零不代表
通话期间从未发送。此修复尚不能证明实际 ICE 超时根因，须保留通话期间摘要实测。

P4 `.65` 修正 ESP-Hosted SDIO TX 的队列所有权边界：入队失败时归还本次缓冲、
返回失败且不发布发送通知；发送线程等待失败或空唤醒时不沿用上一包状态，后续有效
包仍可发送。`test_p4_hosted_tx.py` 编译并执行生产驱动入口，覆盖入队失败、首个
空唤醒、发送后空唤醒与等待失败；修复前四项均失败，修复后通过，已注册到
`tools/check.sh`。这些是已有驱动缺陷的可复现边界，尚未证明实板 VoIP 视频实际
触发了它们。仍须验证连续语音/视频切换、视频接收持续性、SDK 发送占用能否回落，
以及挂断后 MQTT 是否持续可用；不能以主机测试通过代替本次通信故障的实板结论。

P4 `.66` 视频采用可放弃策略，保留原通话和语音 owner。设备/微信通话上行 SDK
发送占用达到容量 50%，或视频发送失败时，媒体 owner 暂停摄像头采集和编码；
暂停至少 2 秒且占用降到 10% 以下才恢复，并请求起始关键帧。暂停不重复塞帧，
不改摄像头隐私设置，不取消下行订阅，不因无视频挂断。容量未知时发送失败也会
触发暂停，但无法按未知容量判断水位。H5/AI 发送策略保持原有行为。
`test_p4_video.py` 通过实际视频入口验证暂停、持续拥塞不重启、不反复调用 SDK、
恢复关键帧、隐私关闭及新代次隔离；修复前在发送失败仍继续发送的断言失败。
`test_p4_video_stale.py` 驱动实际 UI tick：3 秒无新画面隐藏旧图并归还显示槽，
新画面恢复显示，换代或离页也归还旧图。修复前在超时仍显示旧图的断言失败。
两项在 `tools/check.sh` 注册。硬件仍须验证视频停发期间语音连续性、摄像头停止
与关键帧恢复、无下行视频的页面及 MQTT；SDK 既有积压不会被此策略强行清空，
底层通信若已失效，也不能保证语音可继续传输。

P4 `.67` 修正 SDIO streaming RX 批次长度与 HAL 的 16 位参数不匹配：原实现将
32 位整批长度直接传给 `uint16_t size`，64 KiB 会变为 0，随后却按整批推进接收
计数。改为最大 65,024 字节的块对齐分段，每段 FIFO 地址按本段请求长度计算，
整批收齐后仍一次交给原拆包逻辑，不能在分段边界丢弃半包。字节/块两种模式的
实际生产读取循环均通过 `test_p4_hosted_rx_transfer.py` 回放 1、160、65,024、
65,025、65,535、65,536、65,537、131,072、262,144 字节；旧代码在 65,536
字节的零长度传输断言失败，修复后全部字节与末端保护区正确，已注册到
`tools/check.sh`。实板尚须确认是否出现 `SDIO RX large batch split`，以及连续
微信视频后语音、MQTT、AI 是否可用；本测试证明长度缺陷，不替代本次故障的
实板因果验证。协议依据：Espressif `Communication with ESP SDIO Slave` 中
FIFO 地址等于 `0x1F800 - requested length`，单次请求不能超过当前可读长度。

### P4 VGA 下行直转（build.73）

`tools/tests/test_p4_direct_i420.py` 从生产公开转换入口回放官方 imgfx/PPA
SDK 边界：640×480 原尺寸无旋转绕过 pack/PPA/swap，验证 RGB565 两种字节序
配置、句柄复用及释放，旋转/小尺寸/未启用/库失败继续回退 PPA。
修改前该入口仍进入 PPA，回归断言失败；修改后通过并注册 `tools/check.sh`。
主机桩不执行乐鑫 P4 汇编库，像素颜色、并发性能和音频双讲仍需 build.73 实板验收。

`test_p4_video_scheduling.py` 回放生产解码/转换循环的帧结束边界，
在持续积压、5 fps 输入下验证两个工作线程均执行正 tick 休眠，
较长窗口按墙钟而非每 30 帧触发。已接入 `tools/check.sh`。
该测试保护让出 CPU 的机会，不模拟闭源解码器与双核 FreeRTOS；
IDLE1 看门狗消失、音频溢出及双向画面流畅度仍需 build.78 实板验证。

`test_p4_video_submit_probe.py` 通过生产调试控制和 P4 编码完成回调，验证
暂停提交仍执行帧回调、采集观测继续、SDK 发送及成功/失败计数不虚增；
恢复等待关键帧、重复 resume 不打断正常提交、连接清理复位、非设备呼叫拒绝控制。
测试已注册至 `tools/check.sh`。采集/编码线程真实持续运行及卡顿对照仍需实板验证，
一次通话操作步骤见音视频诊断文档 build.79 章节。

P4 build.80 修正 SDIO TX 等待 C6 接收缓冲时持有收发共用总线锁的问题：
保留现有轮询、重试次数和容量记账，在原有 1 ms 等待期间释放锁，等待结束重新
加锁。`test_p4_sdio_tx_fairness.py` 执行生产缓冲查询入口，旧实现因休眠期间
仍持锁而失败；新实现验证等待窗口允许 RX 取得总线、寄存器查询仍串行、缓冲
耗尽返回时持锁、补充缓冲后恢复以及缓存额度不会重复消费。已注册统一检查。
此测试证明消除了持锁休眠，不能证明实板确实触发或已解决秒级视频断流。
硬件须用 build.80 双向 640×480 通话对照 build.79，检查输入长间隔、SDK
发送积压和音频溢出增量；采集、编码、显示、码率和 Wi-Fi 参数未同时调整。

P4 build.82 增加只读 `board-probe` 和 SDK 传输初始化白名单日志。
`test_p4_board_probe.py` 执行生产探针入口，验证通话中拒绝 RPC、未联网跳过、
版本查询失败或 0.0.0 明确输出 unknown、已知版本不冒充完整 APP 身份；
执行 SDK 日志回调，验证未知格式不读取变参、不泄露原文，TGMP/KCP/失败
三种格式只输出允许字段。新增前因生产入口缺失失败，新增后通过，并注册
`tools/check.sh`。保留现有媒体回归覆盖 unchanged 640×480 收发行为。
真机尚须核验板卡修订、Flash/PSRAM、C6 RPC 兼容性及每次建连 TP 日志。
配置引脚、CPU 频率和 Wi-Fi protocol bitmap 均不是实际接线/协商速率证据。

P4 build.85 提高设备通话目标至 VGA/8 fps，固定 150 kbps 沿用 build.84。
`test_p4_fixed_call_bitrate.py` 执行生产配置、采集间隔和 GOP 计算，
先因实际仍为 5 fps 失败，再验证 8 fps、125 ms、GOP16（2 秒）及固定
码率范围。现有 full-frame uplink 回归验证采集/RTC 策略和跨场景复位；
测试均由 `tools/check.sh` 执行。实际双向显示帧率和声音需要两板验证。

P4 build.86 两档上行由 `test_p4_video_quality.py` 执行真实媒体 owner 控制器
及 governor 档位配置：持续积压/带宽不足降档、发送失败立即降档、健康
30 秒恢复、短暂积压不切换、反馈过期、SDK 不支持反馈时本地保护、隐私
暂停计时、陈旧连接拒绝及新会话复位。新增前生产接口缺失失败，新增后通过。
同测试执行生产目标尺寸选择，验证 QVGA 可恢复 VGA、资源 fallback 和输入
尺寸边界，并核对摄像头 worker 使用该选择器；已注册 tools/check.sh。
原配置/采集/RTC 策略回归改为两档高档 5 fps，验证 200 ms 和 GOP10。
实际 PPA 缩放、编码器重建后 SPS/PPS/IDR、接收端动态分辨率、VoIP 兼容性
及持续双向音频均待实板验证；主机测试通过不表示已完成硬件验收。

P4 build.87 将 CALL/VoIP 两档控制改为本地积压/失败判断，取消 SDK 码率
范围配置与建议消费，以隔离 build.86 相比稳定基线引入的传输控制变量。
`test_p4_video_quality.py` 先在生产 owner 调用 SDK 范围入口处断言失败，
再验证该入口不被调用、SDK 建议不能改变健康链路档位，同时保留拥塞
降档/健康恢复/隐私/会话边界回归。主机测试不证明 SDK 配置是卡顿根因；
两端 build.87 的完整双向通话和切档仍待硬件验证。
