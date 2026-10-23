# BK7258 上板验收

本文中的“上板验证”表示由操作人员连接实物、烧录固件并观察结果。它不是开发
阶段审批，也不要求逐项回复后代码才能继续。

历史版本已在实板跑通 H5 双向音频和 H.264 视频、AI 对话及音频微信 VoIP。
本轮用户已确认 BK7258 当前使用正常；各场景的证据与镜像身份分别记录，历史通过不代表新镜像已逐项验收。

实际结果按 PASS / FAIL / SKIP 记录，并保留本次固件身份与日志。

## iPhone 配网页诊断版 build.70

本地 `1.0.1+build.70` 仅增加临时配网日志，保留原启动顺序、DNS 和 HTTP 响应内容。
完整串口镜像：`build/bk7258/lckfb-bk7258-xiaotai/package/lckfb-bk7258-1.0.1+build.70-all-app.bin`。
SHA-256：`ea671a0750944b5da87833a90bd7893ab57cc0af250714163e0da47fe00e59c3`。
用户已提供 build.70 启动身份及实板日志，确认本次 iPhone 自动弹出配网页。
热点启动后屏幕提示调用耗时约 5 ms，网页服务在配网开始后 22 ms 就绪；
收到 Apple 探测并返回 302，随后页面与扫描接口正常响应，扫描得到 13 个网络。
日志未见明显启动阻塞；此前未弹窗的根因尚未确认，不能将本次成功表述为根因已修复。

1. 烧录后，在进入配网前开始保存串口日志，确认启动版本为 build.70。
2. 使用同一部 iPhone 连接立创热点，先等待约 15 秒，记录是否自动弹窗。
3. 若未弹窗，再手动打开 `http://192.168.6.1`，保留打开前后的日志。
4. 对比 `[DEBUG-bk-portal]` 的 `ap-started`、`ui-returned`、`ready`、
   `accepted`、`request` 和 `response`；发送成功不代表手机已显示页面。

日志只记录固定请求类别、状态和耗时，不记录密码、请求头、正文或任意 URL。
该组日志不覆盖 DHCP/DNS 报文，必要时再针对缺失环节取证。

## 准备

- 固件：`build/bk7258/lckfb-bk7258-xiaotai/package/all-app.bin`
- 默认 DEBUG 固件会完整打印 HTTP、MQTT、WHIP 请求和返回值（包括 token），
  用于开发联调，日志按敏感开发资料处理；切换到 INFO 后不打印完整网络载荷。
- 使用 BK7258 配套烧录工具执行接线、Boot 键和复位操作；工程不自动控制硬件。

## 本轮跨板复测

日期：2026-10-23。对比基线为立创已发布 `bk7258-v1.0.1`（build.56）。
最近的铃声修复为 `f17ac88`；本候选来自 `9dc9a01`。代码目录迁移与功能变化分开核对，
避免把文件移动当成硬件实现未变的证据。已发布 build.56 的附件没有被替换；从当前源码
重新编译的立创固件会包含下列共用改动。

### 候选固件

| 项目 | 本轮记录 |
|---|---|
| 板卡 | `lckfb-bk7258`，`default` / `touch-full` / `compact` |
| 版本 | `1.0.1+build.69`，release / INFO |
| 构建源码 | `9dc9a016089ee1caa99e9628199121b816a3393f` |
| 串口烧录文件 | `lckfb-bk7258-1.0.1+build.69-all-app.bin`，起始地址 `0x0` |
| 完整 BIN SHA-256 | `e71a6a7a0ab9b8cbc25e0356c204c367cf4b70e285d8a8bb43f5070bfc03dab8` |
| 自动检查 | 467 项主机回归、ASan/UBSan、6 板清单校验通过；本板 AP/CP 编译与契约检查通过 |
| 实际编译隔离 | 核对本候选 compile_commands.json：仅包含立创板级源文件，不含觅感双眼/无屏 UI，未启用关闭设备互呼或房间的觅感专属宏 |
| 实板状态 | 待烧录，所有以下结果均为待测；觅感结果不能代替本板结果 |

使用本工程固定目录 `build/bk7258/lckfb-bk7258-xiaotai/package/` 的本候选。
普通 Download 保留网络、绑定与设置，先测试旧设置兼容；全片擦除与重绑定留到最后。
本候选用于回归验证，未替换立创在线稳定发布。

### 改动与受影响功能

| 范围 | 相对基线的变化 | 立创实板重点 |
|---|---|---|
| 公共工程与板级边界 | runtime、构建脚本、SDK 补丁共用；音频设备控制提取到 `xiaotai_onboard_audio.c`，功放引脚和极性仍由本板适配 | 上电默认无误响；AI、H5、微信、设备呼叫、房间 PTT 均能独立打开并释放扬声器 |
| 呼叫与铃声 | 快捷键改为首个微信联系人；新增呼入/呼出持续铃声、接通交接互斥、阻塞写入唤醒及停止重试 | 混合列表不误拨设备；铃声连续；双向声音；拒接、取消、超时后可重拨 |
| AI 音频 | build.56 的 Opus / 16 kHz 上行改为 A-law / 8 kHz；下行 Opus / 16 kHz，硬件 AEC/NS/AGC 仍为 16 kHz | 识别与回答、说话开头、尾音、连续问答、打断和 AI 转拨 |
| 触摸 UI 与表情 | AI 历史页、联系人每页三项、设置页与固定表情选择等公共页面有变化 | 控件与触摸位置一致；翻页、返回、表情和 AI Tag 正常；PTT 按住/松开正确 |
| 设置存储 | 兼容旧记录长度、灵敏度及表情索引校验 | 直接升级保留原音量、静音、灵敏度与表情；修改后重启仍保持 |
| SDK 网络 | 新 CP 核配网 DNS 应答与共用补丁 | iPhone 热点自动弹窗、手动门户、保存联网和再次配网；最终再测试清配置 |
| 功能边界 | 觅感关闭设备互呼、多人房间和触摸的开关仅在觅感入口设置；立创仍选择原完整交互 | 本板设备互呼、房间与触摸功能实际可用，不能只检查代码开关 |

板级对比确认：本板 `board_config.h`、`board_camera.c`、`board_display.c`、`board_touch.c`、
`board_button.c` 没有相对发布基线的改动；公共 `xiaotai_video.c` 内容也相同。
共用音频与网络仍可能影响 H5 联合运行，因此这些功能仍需实板测试。

### 先做这一轮

先全程保存串口日志。每项开始和结束时记录实际时间；听感由操作人员确认。

| 顺序 | 操作与通过条件 | 结果 |
|---|---|---|
| 1 | 烧录本候选、启动到 READY，发送下方状态命令，核对版本、联网、绑定和空闲内存 | 待测 |
| 2 | 微信呼出与呼入各 3 次；主被叫双方分别挂断；等待期间持续响铃，接通后双方轮流说话，再马上重拨 | 待测 |
| 3 | AI 进入/退出 10 次、每次至少两轮；至少一次播放中结束，再让 AI 呼叫微信联系人 | 待测 |
| 4 | H5 画面与双向声音，正常关闭、直接关网页各 3 次；再次打开均正常 | 待测 |
| 5 | AI 和 H5 期间微信来电，分别拒接、接听；拒接保留原业务，接听后正确切换且仅一个媒体 owner | 待测 |
| 6 | 设备互呼的主叫/被叫双向声音；房间收听/PTT/退出/重进；缺对端时写 SKIP，不算通过 | 待测 |
| 7 | 联系人和 AI 历史翻页、设置/表情、屏幕返回、PTT 抬手；保留旧设置并修改重启验证 | 待测 |
| 8 | 预热 3 轮后重复混合业务 30 轮，比较每轮结束 10 秒后的空闲内存和任务数；不能持续下降或出现停止超时 | 待测 |

```text
ap_cmd status
ap_cmd call-status
ap_cmd room-status
```

首次回传先提供第 1–2 项：完整启动版本、一次呼出、一次呼入、双方听感和结束后的状态。
之后继续第 3–8 项。出现无声、卡住、重启或持续内存下降时，保留发生前后日志并先定位该项。

## 一次完整验收

### 完整功能复测清单

本次为 `touch-full / compact` 开发验证包，尚未完成本版本实板验收。先在包内
`MANIFEST.json` 核对完整版本和 SHA-256，再核对启动日志的版本。
保留已有网络和绑定即可先测核心功能；清除配置与重新绑定放在最后，避免中断前面的测试。
设备呼叫、微信 VoIP 和多人对讲按纯音频验收；H5 验收 H.264 视频上行与双向音频。

逐项填写 PASS / FAIL / SKIP，SKIP 写明缺少的对端或条件。每轮记录实际双方声音，
不能仅凭“连接成功”或 HTTP 200 判通过。

| 编号 | 操作 | 通过条件 | 结果 |
| --- | --- | --- | --- |
| B01 | 冷启动 3 次，再待机 30 分钟 | 版本一致；自动联网、时间正确、READY；平台在线；无断言、心跳超时或意外重启 | 待测 |
| U01 | 首页、菜单、联系人、多人对讲、表情、设置、运行状态逐页进入返回 | 触摸位置正确、控件间隔不误触、文字可读；返回不会误启动 AI | 待测 |
| U02 | 待机 KEY 单击/双击；首页微信快捷入口；分别使用混合联系人列表和无微信联系人列表 | 单击 AI；双击和首页快捷入口均选平台顺序中的首个微信联系人，跳过设备；无微信联系人时显示添加引导 | 待测 |
| U03 | 无联系人、重复姓名、离线联系人；重新进入联系人页 | 无目标时显示引导；歧义不误拨；离线反馈可退出；列表按平台顺序刷新 | 待测 |
| H01 | H5 打开画面，双方轮流讲话，再双向讲话 | 有连续正向画面，设备→网页和网页→设备均有声音 | 待测 |
| H02 | H5 正常退出、直接关页面，各重复 5 次 | 摄像头/音频停止；下一次能重开；无永久 busy、旧声音或重启 | 待测 |
| A01 | AI 连续 3 轮问答，重复进入/退出 10 次 | 识别、字幕、表情与播放正常；结束后回到 READY，可再次启动 | 待测 |
| A02 | AI 回答中本地结束；让 AI 主动结束；结束后马上重开 | 本地停止生效；远端结束尾句完整且有界退出；新会话不被旧清理关掉 | 待测 |
| A03 | AI 指令呼叫唯一设备联系人、唯一微信联系人，再试同名/未知目标 | 动作结果先回传，旧 AI 释放后正确转拨；同名/未知不误拨、不误结束 AI | 待测 |
| C01 | 设备互呼：本板主叫和被叫，双方分别挂断，各 3 次 | 接通后双向声音；挂断回 READY；第二次通话可用 | 待测 |
| C02 | 设备呼叫：接听、拒绝、振铃时取消、不接听超时 | 双方状态收敛；超时后可重拨；迟到回调不重新开麦 | 待测 |
| W01 | 微信呼入/呼出，接听后双方讲话，本地/微信分别挂断，各 3 次 | 确认后双向声音；挂断恢复；重复通话无单向音频或永久忙 | 待测 |
| W02 | 微信拒接、未接超时、外呼后立即取消 | 结果正确；迟到回铃不变成新来电；之后可再呼叫 | 待测 |
| X01 | H5、AI、房间收听期间分别呼入，先拒接再接听 | 来电提示不立即切断原业务；拒接/超时保留原业务；接听才释放原媒体并切换 | 待测 |
| X02 | 通话中再来电；H5→AI→设备通话→微信→房间→H5，循环 3 轮 | 每次只存在一个媒体 owner；忙时反馈合理；无旧音频、误停止、串会话或重启 | 待测 |
| R01 | 无密码创建、有密码创建、加入含前导零的房间号/密码、错误密码 | 分组和键盘正确；不足位数不提交；服务端失败不显示成功 | 待测 |
| R02 | 与另一成员收听/PTT；连接前松手、持续静止按住、拖出后松手 | 连接确认后才发言；提前释放不自动开麦；静止按住不误释放；松开停止上行、保持收听 | 待测 |
| R03 | 多人对讲左上返回、重进；退出房间先取消，再确认 | 返回仅断媒体且保留分配；重进可连接；取消不退出，确认后服务端分配与本地媒体均清除 | 待测 |
| R04 | Web 修改分配；离线时修改后恢复网络，再显式进房间页；超过 3 名成员翻页 | MQTT 或显式进页刷新；待机不周期查分配；成员人数/页码正确，PTT 不因刷新失效 | 待测 |
| S01 | 设置音量 0–10、灵敏度 1–5、麦克风/扬声器开关、息屏和回应声，再重启 | 端点不循环、保存后保持；静音实际生效，恢复后有声音；唤醒首触不误操作 | 待测 |
| N01 | READY、AI、H5、通话中分别断开 Wi-Fi 30 秒再恢复 | 不崩溃；旧会话有界结束；MQTT 恢复，后续 AI/呼叫/H5 可重新发起 | 待测 |
| L01 | AI、设备通话、微信各启停 30 次；H5 连续 30 分钟 | 无断言/重启/卡死；结束后任务和可用内存趋于稳定，不逐轮下降 | 待测 |
| L02 | 完成上面流程后待机至少 2 小时 | 心跳、MQTT 持续，平台在线；无异常重启或持续内存下降 | 待测 |
| B02 | 最后测试“重置设备”：先取消，再确认；重新配网、验证码绑定、重启 | 取消保留配置；确认清除本机网络/绑定/设置，保留固件和校准；重新绑定后业务可用 | 待测 |

故障回传：编号、完整固件版本、操作时间、对端类型、谁听不到谁/谁看不到画面、
从操作前到失败后的串口日志，以及是否可重试恢复。从 CP 串口输入 `ap_cmd status`、
`ap_cmd call-status`、`ap_cmd room-status` 辅助采样。CPU=-1、RSSI=-128、栈低水位 0 或
largest=null 不能当作有效测量；同一版本预热后比较 READY 时的 free 内存和任务数。
当前主机回归覆盖协议和资源交接，不替代真实 ADC/DAC、摄像头、触摸及平台互通验证。

### 基本操作与日志对照

1. 烧录 `all-app.bin` 并复位设备。
2. 首次启动时连接 `XiaoTai-XXXX` SoftAP，访问 `http://192.168.6.1`，输入
   Wi-Fi SSID 和密码。
3. 设备重启并联网后，在小钛平台输入设备显示或播报的验证码。
4. 等待设备完成 TiRTC 启动、MQTT 登录和能力上报。
   空闲首页应显示北京时间、日期、默认表情和当前 Wi-Fi 信号格数。
5. 空闲状态按一次 KEY，确认进入 AI 对讲；再按一次，确认退出。
   执行前确认平台和 TiRTC 就绪，不等待后台 assignment 轮询。需要验证房间分配时，通过打开多人对讲页或 MQTT 通知触发查询。随后连续完成
   10 次“按键进入、按键退出”，全程不得出现 `UsageFault`、`HardFault` 或自动重启。
6. 在触摸首页点击右下角三个点，必须进入“功能菜单”，不得启动 AI；点击首页
   其他空白区域才启动 AI。再分别点击四角附近的控件，确认横屏方向没有旋转或镜像。
7. 从小程序打开远程查看，再开启双向语音。
8. 关闭远程查看，重复一次，检查摄像头、麦克风和扬声器能够释放并重新打开。

## 串口通过标志

启动和绑定：

```text
wifi_manager: Wi-Fi connected
xiaotai_tirtc: SDK ready version=
platform_client: MQTT connected and device topics subscribed
```

打开远程查看：

```text
xiaotai_tirtc: inbound STREAM accepted generation=
xiaotai_audio: amplifier power GPIO8=1 play GPIO23=1 (muted)
xiaotai_audio: amplifier play GPIO23=0 playing=1
xiaotai_audio: full-duplex board audio started
xiaotai_tirtc: audio subscription stream=10 supported=1 active=1
xiaotai_tirtc: video subscription stream=11 supported=1 active=1
xiaotai_video: native H264 camera started 640x480 sensor=20 fps uplink=20 fps
xiaotai_audio: BK SDK AGC ready rate=8000 frame=160 target=-3dBFS compression=16dB limiter=1 heap=...->...
xiaotai_audio: SDK AGC level in=... out=... limited=... failures=...
xiaotai_audio: microphone sensitivity=4 adc-gain=0x35 rc=0
xiaotai_audio: first uplink frame sent bytes=160
xiaotai_video: first H264 frame sent bytes=
```

小程序向设备讲话后：

```text
xiaotai_audio: first downlink frame played bytes=320
```

按钮 AI：

```text
xiaotai_button: KEY pressed; product action queued
xiaotai_app: manual AI request submitted generation=
xiaotai_tirtc: AI WHIP connected generation=
xiaotai_audio: BK SDK NS ready rate=16000 frame=320 suppression=-25dB
xiaotai_audio: BK SDK AGC ready rate=16000 frame=320 target=-3dBFS compression=16dB limiter=1 heap=...->...
xiaotai_app: AI start_session sent; microphone remains closed
xiaotai_app: AI talk active generation=
xiaotai_audio: SDK NS frames speech=... noise=... failures=0 suppression=-25dB
```

讲话和收到回复时，日志应依次出现 `ASR caption`、`ASR caption final`、
`AI caption`；屏幕在 `LISTENING`、`THINKING`、`SPEAKING` 间切换并显示对应
表情。底部显示最新两行 UTF-8 字幕，以“你：”或“AI：”区分说话方；
基本 CJK、英文和常用符号应直接显示，范围外字符显示方框且不得崩溃。

关闭远程查看：

```text
xiaotai_video: camera stopped sent=
xiaotai_audio: amplifier play GPIO23=1 playing=0
xiaotai_audio: amplifier power GPIO8=0
xiaotai_audio: audio stopped up=
xiaotai_tirtc: remote STREAM disconnected generation=
xiaotai_tirtc: connection closed mode=1 generation=
```

微信对讲（当前为音频模式）：

```text
xiaotai_app: VoIP incoming room=...; press KEY to answer
xiaotai_app: KEY accepted VoIP room=...
xiaotai_tirtc: VoIP WHIP connected generation=...; waiting command 0x2000
xiaotai_app: VoIP transport connected; waiting platform accept
xiaotai_app: VoIP audio active generation=
```

通话中再次按 KEY 时应发送 `0x2001` 并返回 `READY`。远端发送 `0x2001` 或
MQTT `call_cancel` 时也应释放音频。已有待接听或一对一通话时，后到的微信来电应调用
`/v1/wxvoip/reject`。AI、H5 或多人对讲占用媒体时，第一条来电只进入待接听槽；
原业务继续，用户接听后才切换媒体。拒接或来电超时不能中断原业务。

AI 联系人外呼：先确认平台通讯录中存在唯一备注名，再在 AI 会话中说“呼叫
<备注名>”。设备联系人应依次出现：

```text
xiaotai_app: contact cache replaced count=
xiaotai_app: AI call intent accepted name=... type=1
xiaotai_app: outbound device call waiting room=... target=...
xiaotai_tirtc: outbound device call transport accepted generation=... room=...
xiaotai_app: device call media active generation=... type=audio
```

平台下发 `device_action` 时，设备必须先成功发送带原 JSON-RPC `id` 的动作结果，
再出现 `AI call intent accepted` 并释放 AI 媒体。通话页左侧矩形按钮应显示当前状态
“麦克风已开”；点击后显示“麦克风已关”，再次点击恢复。

微信联系人对应 `type=2`，应出现 `outbound VoIP waiting call_id=`、
`outbound VoIP answered call_id=`，收到 `0x2000` 后才出现
`VoIP audio active`。同名、未知联系人和非 audio 媒体不能结束 AI 或拨号。

超时恢复：来电 45 秒未接听，或接听/AI 建连后 15 秒未完成协议确认，应出现：

```text
xiaotai_app: session timeout owner=... state=... generation=...
```

屏幕显示 `TIMEOUT`，旧 generation 随后的成功回调不能开启媒体；再次按 KEY
应可重新发起 AI。

## 结果判定

| 项目 | 通过条件 | 失败线索 |
| --- | --- | --- |
| 配网与绑定 | 重启后自动联网，MQTT 保持在线 | 重复进入 SoftAP、绑定凭证未保存 |
| 空闲首页 | 时间与北京时间一致，RSSI 强弱变化反映为 1–4 格，右侧不显示 dBm 数值 | 时间仍为 1970/异常年份、信号图标固定不变、状态页被首页立即覆盖 |
| 触摸方向 | 首页右下三点进入功能菜单；菜单六宫格、顶部返回和设置控件均与显示位置一致 | 右下三点启动 AI、四角颠倒、左右或上下镜像 |
| 上行音频 | 小程序能听到设备麦克风，出现首帧日志 | `uplink not ready` 持续增长 |
| 下行音频 | 设备能播放小程序讲话，出现播放首帧日志 | 没有下行首帧、环形缓冲持续丢旧数据 |
| 功放控制 | P8 上电后 P23 拉低才出声；停止时 P23 先拉高、随后 P8 断电，无明显爆音 | P8 极性相反、P23 电平与播放状态相反、停止后功放仍耗电或爆音 |
| 远程视频 | 小程序显示连续的 640×480 画面 | 摄像头创建失败、没有 H.264 首帧 |
| 视频方向 | 以设备当前 UI 正方向观察，画面保持正向 | 上下颠倒时记录需要旋转 180°，左右错误单独记录镜像 |
| 会话释放 | 关闭后出现三条停止日志，并可再次打开 | sender/audio stop timeout、第二次连接被误判为忙 |
| 按键 AI | 平台就绪后连续 10 次进入/退出；KEY 首次按下进入 AI，再按退出；不依赖 assignment 轮询 | `xiaotai_control` UsageFault、token/WHIP 失败、确认前麦克风已启动、退出后音频仍占用 |
| AI 状态显示 | ASR/TTS 事件驱动监听、思考、回答；依次注入平台十个中文 emotion 值时显示对应本地表情并出现 `AI emotion changed` 日志；本地表情页可浏览全部 21 个兼容 tag；快速 partial 字幕不耗尽显示缓冲 | 中文 emotion 被当成未知值、表情无变化、一直停在 LISTENING、连续出现 frame allocation failed |
| AI 收音与降噪 | 在安静环境和持续风扇噪声下分别以 0.5 m、1 m、2 m 正常音量说话；出现 16 kHz AGC/NS ready 日志，`SDK NS` failures 保持 0，AGC 输出高于低电平输入且无持续限幅，服务端连续识别 | 无 NS、正常人声被过度抑制、远距离仍频繁识别不到、输出长期为零或持续限幅 |
| 麦克风灵敏度 | 设置页 1–5 档增减、到达端点不循环并重启保留；3 档日志为 `adc-gain=0x2d`，4 档为 `0x35`；分别复测 H5、AI、设备呼叫、微信 VoIP 和多人对讲，远讲改善且近讲无持续限幅/破音 | 设置不生效或重启丢失、只影响部分模式、VoIP 过响、近讲 ADC 削波或底噪明显上升 |
| 房间触摸恢复 | 按住说话并保持静止不应出现合成抬起；制造一次 FT6336 读取失败后只出现一次 `synthesized release after controller read failure`，上行变为 0，下一次左上返回和“退出房间”均可用 | 空事件队列触发假抬起、失败期间重复合成、一直显示“正在说话”或后续按钮无响应 |
| AI 结束尾音 | 让角色以“我先退下了”等完整句结束；收到 `end_session` 后日志显示 `arrival-grace=1500`，完整播放最后一句后主动断开，整个流程最长 5 秒；若远端先关闭，则出现 `AI playback after transport close drained` 并在 3 秒内回到首页 | 只听到“我会/我先”等前半句、未保留尾音到达窗口、播放任务停止后仍等待、超过对应上限不释放 |
| 反复结束稳定性 | 连续执行 AI、设备通话、VoIP 启停各 30 次；每次回到 `READY`，且无 CP `MemFault`、`__skb_unlink`、IPC heartbeat timeout、assert 或重启 | 任一发送队列断链、访问地址 `0x4`、卡在结束态或自动重启 |
| 微信对讲 | KEY 接听后收到 `0x2000` 才启用音频；本地或远端挂断后回到 READY | 未收到平台确认就开麦、忙时未拒接、结束后音频仍占用 |
| AI 联系人外呼 | 本地唯一匹配，AI 音频先释放；设备/微信链路均在 `0x2000` 后开麦 | 任意 ID 可绕过通讯录、同名误拨、迟到微信回铃变成新来电 |
| 超时恢复 | 待接 45 秒、连接/确认 15 秒到期后显示 TIMEOUT，资源释放且可重新发起 | 永久停在 CONNECT、迟到回调重新开麦、后续请求一直 busy |

方向、音量和回声必须根据实物结果调整。未出现对应首帧日志时，应先定位数据链
路，不能只根据小程序画面或声音主观判断。

首次验证功放时，用万用表或示波器记录 P8/PLAY_PWR_CTL 的开关电平。当前软件按
高电平供电处理，这是基于网络名称的待验证假设；P23/PLAY_CTL 则按已提供信息固定
为低电平播放、高电平关闭。若 P8 实测为低有效，先修正极性再继续声音测试。
