# BK7258 上板验收

本文中的“上板验证”表示由操作人员连接实物、烧录固件并观察结果。它不是开发
阶段审批，也不要求逐项回复后代码才能继续。

项目方已经确认当前工程在实板跑通 H5 双向音频和 H.264 视频、16 kHz Opus AI 对话，
以及音频微信 VoIP。本页用于后续固件重复验收，不记录历史故障过程。

TODO：下一次发布候选固件验收后，补充固件 SHA-256、验证日期和完整串口日志。
这项证据补充不阻塞当前源码提交。

## 准备

- 固件：`build/bk7258/lckfb-bk7258-xiaotai/package/all-app.bin`
- 默认 DEBUG 固件会完整打印 HTTP、MQTT、WHIP 请求和返回值（包括 token），
  用于开发联调，日志按敏感开发资料处理；切换到 INFO 后不打印完整网络载荷。
- 使用 BK7258 配套烧录工具执行接线、Boot 键和复位操作；工程不自动控制硬件。

## 一次完整验收

1. 烧录 `all-app.bin` 并复位设备。
2. 首次启动时连接 `XiaoTai-XXXX` SoftAP，访问 `http://192.168.6.1`，输入
   Wi-Fi SSID 和密码。
3. 设备重启并联网后，在小钛平台输入设备显示或播报的验证码。
4. 等待设备完成 TiRTC 启动、MQTT 登录和能力上报。
   空闲首页应显示北京时间、日期、默认表情和当前 Wi-Fi 信号格数。
5. 空闲状态按一次 KEY，确认进入 AI 对讲；再按一次，确认退出。
   执行前至少等待两次 `/v1/call/group/device/assignment` 成功响应，随后连续完成
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
MQTT `call_cancel` 时也应释放音频。其他会话正在占用媒体资源时，新来电应调用
`/v1/wxvoip/reject`，日志出现 `VoIP rejected busy room=...`。

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
| 按键 AI | assignment 轮询后连续 10 次进入/退出，未崩溃；KEY 首次按下进入 AI，再按退出；未运行唤醒词任务 | `xiaotai_control` UsageFault、token/WHIP 失败、确认前麦克风已启动、退出后音频仍占用 |
| AI 状态显示 | ASR/TTS 事件驱动监听、思考、回答；依次注入平台十个中文 emotion 值时显示对应本地表情并出现 `AI emotion changed` 日志；本地表情页可浏览全部 21 个兼容 tag；快速 partial 字幕不耗尽显示缓冲 | 中文 emotion 被当成未知值、表情无变化、一直停在 LISTENING、连续出现 frame allocation failed |
| AI 收音与降噪 | 在安静环境和持续风扇噪声下分别以 0.5 m、1 m、2 m 正常音量说话；出现 16 kHz AGC/NS ready 日志，`SDK NS` failures 保持 0，AGC 输出高于低电平输入且无持续限幅，服务端连续识别 | 无 NS、正常人声被过度抑制、远距离仍频繁识别不到、输出长期为零或持续限幅 |
| 麦克风灵敏度 | 设置页 1–5 档可循环并重启保留；3 档日志为 `adc-gain=0x2d`，4 档为 `0x35`；分别复测 H5、AI、设备呼叫、微信 VoIP 和多人对讲，远讲改善且近讲无持续限幅/破音 | 设置不生效或重启丢失、只影响部分模式、VoIP 过响、近讲 ADC 削波或底噪明显上升 |
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
