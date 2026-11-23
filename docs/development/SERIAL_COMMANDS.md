# ESP32 与 BK7258 串口命令

[中文](SERIAL_COMMANDS.md) · [English](SERIAL_COMMANDS.en.md)

从 v1.2.0-beta.3 起，两款 ESP32-S3、微雪 ESP32-P4 与两款 BK7258 使用下表的通用维护命令。旧固件请以本机帮助为准。

## 连接与输入

按对应开发板指南连接，关闭烧录工具后再打开串口终端。UART 使用 115200 波特率、8 位数据、无校验、1 位停止位。每行输入一条，按回车发送。

| 终端 | 查看命令 | 查状态示例 |
|---|---|---|
| ESP32 应用终端，提示符 `starter>` | `help` | `status` |
| BK7258 常用 CP 串口，转发到 AP | `ap_cmd help` | `ap_cmd status` |

下文统一写应用命令。**BK 每一行前加 `ap_cmd `，ESP32 直接输入。** BK 直接输入 `help` 会列出 CP 命令。尖括号表示替换值，方括号表示可选，名称含空格时用英文双引号。返回 requested 表示已提交，完成状态看随后日志和状态查询。

## 查看状态与日志

| 命令 | 用途 |
|---|---|
| `version` | 查看固件身份和链接的 TiRTC SDK 版本。 |
| `status` | 查看联网、TiRTC 和当前会话；字段按平台有所不同 |
| `call-status` | 查看通话类型、状态和代次；BK 另报超时时刻 |
| `log-level` | 查看当前应用及 TiRTC 日志级别 |
| `log-level app <0..4>` | 调整应用日志级别：0 关闭、1 错误、2 警告、3 信息、4 调试 |
| `log-level tirtc <0..17>` | 调整 TiRTC SDK 日志级别，取值由 SDK 定义，不沿用应用日志的 0..4 含义 |

BK 的 `status` 中，`owner` 表示当前业务，`pending` 表示待处理来电，`state` 是运行时状态编号。
`internal_free` / `internal_min` 是内部 SRAM 当前剩余量 / 启动以来最低剩余量，
`psram_free` / `psram_min` 是外部 PSRAM 的对应数值，单位均为字节。
`bound=1` 表示本机已保存设备凭证，还需结合平台状态确认服务正常。

通常保留信息级别：

```text
log-level app 3
```

BK 采集周期性的 `METRICS` 和视频帧率统计前，先运行 `log-level app 4`。

排查时可临时使用 `log-level app 4`。日志级别不保存，重启后恢复固件默认值；
编译时关闭的 SDK 日志不能靠此命令恢复。分享日志前删除密码、令牌等敏感内容。

## 配网、绑定与重启

| 命令 | 用途与结果 |
|---|---|
| `wifi-change` | 断开当前 Wi-Fi，打开配网热点，保留已保存的 Wi-Fi 信息 |
| `wifi-set <SSID> <密码>` | 保存 Wi-Fi 并重启；开放网络的密码填 `""` |
| `wifi-clear` | 清除已保存的 Wi-Fi 并重启，重新进入配网 |
| `tirtc-set <device_id> <device_secret> [client_id]` | 联调时预置凭证并重启。凭证可能留在终端记录中。 |
| `tirtc-clear` | 清除本机绑定凭证并重启，重新进入绑定；不清除 Wi-Fi |
| `restart` | 重启设备，保留已保存的设置 |

更换网络前，先结束 AI、查看或通话，再执行 `wifi-change`。手机连接设备热点后，
打开 `http://192.168.6.1` 填写 Wi-Fi 信息。也可直接通过串口保存：

```text
wifi-set "Home WiFi" "your-password"
```

请替换成实际名称和密码；串口终端可能保留输入记录。
`wificlear` 是 `wifi-clear` 的兼容别名，完整输入为 `wificlear`。

## AI 与通话

开始 AI 或发起呼叫前，设备需已联网并完成绑定。呼叫联系人还需完成联系人同步；
接听与挂断命令作用于当前来电或通话。

| 命令 | 用途 | 支持的开发板 |
|---|---|---|
| `ai-start` | 开始 AI 对讲 | 五板 |
| `ai-stop` | 结束 AI 对讲 | 五板 |
| `wechat-call ["联系人名称"]` | 呼叫指定微信联系人；省略名称时，选择平台顺序中的首个微信联系人 | 五板 |
| `call-answer` | 接听当前来电 | 五板；觅感仅支持微信来电 |
| `wechat-answer` | 只接听微信来电 | 五板 |
| `call-hangup` | 结束当前通话或取消呼叫 | 五板 |
| `device-call "联系人名称"` | 呼叫指定设备联系人 | ESP32 三板、立创 BK |
| `device-answer` | 只接听设备来电 | ESP32 三板、立创 BK |

例如，呼叫通讯录中的微信联系人“哥哥”：

```text
wechat-call "哥哥"
call-status
```

## 多人对讲（ESP32 三板及立创 BK）

ESP 触摸板在房间页前台连接房间媒体。串口可提交加入请求，发言前请在设备打开房间页，确认已加入，再用发言命令。

觅感不支持多人对讲，即使命令出现在帮助列表中，也不能用它开启该功能。

| 命令 | 用途 |
|---|---|
| `room-status` | 查看分配、连接、加入、发言状态及成员数 |
| `room-sync` | 请求刷新平台房间分配 |
| `room-join <6位房间号> [4位密码]` | 请求加入指定房间 |
| `room-talk-start` | 开始发言 |
| `room-talk-stop` | 停止发言，恢复收听 |
| `room-leave` | 退出房间 |

以上为串口维护入口，不增加产品界面的自动刷新触发条件。
`room-talk-start` 与 `room-talk-stop` 应配对使用；开始发言后需要主动发送停止命令。

## 修改平台服务发现地址

普通配网不需要修改这一项。连接自部署平台或指定测试环境时再使用：

| 命令 | 用途与结果 |
|---|---|
| `ep-show` | 查看已保存的服务发现基址；显示 `default` 表示使用固件默认地址 |
| `ep-set <地址>` | 保存新的服务发现基址并重启 |
| `ep-reset` | 删除覆盖项，恢复固件默认服务发现地址并重启 |

例如，使用带 `/pre` 路径的服务发现基址：

```text
ep-show
ep-set http://ep-open.tangeopen.com/pre
```

重启后设备请求 `http://ep-open.tangeopen.com/pre/services`。
地址必须以 `http://` 或 `https://` 开头，可带端口和路径，不接受查询参数（`?`）或片段（`#`）。
TiRTC 连接地址取自服务发现响应的 `tirtc-srv` 字段，`ep-set` 设置的是服务发现入口。
恢复默认地址时执行 `ep-reset`。

## 时间同步服务器

| 命令 | 作用 |
| --- | --- |
| `ntp-show` | 查看主服务器覆盖项；无有效覆盖项时提示可能未保存、读取失败或损坏 |
| `ntp-set time.example.com` | 保存主服务器域名或 IPv4 地址并重启 |
| `ntp-reset` | 清除覆盖项并重启 |

输入最长 253 字符，不带协议前缀、端口、路径或空格，不支持 IPv6。保留固件原有
备用服务器，NTP 使用 UDP/123。成功修改会中断当前会话；保存失败不重启。
恢复出厂设置也会清除该项。重启后须确认校时成功。

默认主服务器为 `pool.ntp.org`，备用为 `ntp.aliyun.com`。`ntp-show` 只查询保存的
配置，不表示校时已经成功。请将 `time.example.com` 替换为可达服务器。局域网示例：

```text
ntp-set 192.168.1.10
# 等待重启后查询
ntp-show
# 恢复默认并重启
ntp-reset
```

## 板级诊断

| 命令 | 适用范围与用途 |
|---|---|
| `task-diag` | ESP 采样指定任务 5 秒；BK 返回最近一次任务数、CPU 和栈余量采样。 |
| `voice-config <50..990>`、`voice-diag [5..30]`、`voice-test self-test` | ESP 的唤醒阈值、音频电平观察、文字注入自检。 |
| `face-animation [on/off]` | ESP 临时人脸动画开关，实际输入 on 或 off。 |
| `media-diag`、`video-submit` | ESP 开发诊断。前者接 on/off/show/reset；后者接 pause/resume/show，控制视频提交需要微雪 P4 设备通话和诊断构建。 |
| `audio-trace`、`audio-capture` | S3 开启逐包抓取的调试构建。均接 arm/stop/status/dump/clear，空闲时 arm，采集内容可能含私人语音。 |
| `camera-snapshot`、`video-capture`、`downlink-capture` | 微雪 P4 的相机和视频开发抓取，按本机注册情况使用。 |

ESP 的 app 日志级别调整 ESP-IDF 默认过滤器，编译上限会限制可输出的调试信息；TiRTC 有独立级别。BK 的 METRICS 等计数按平台解释。一次 AI 验收可依次执行 `version`、`status`、`ai-start`，连续对话几轮后执行 `ai-stop`、`status`，保留会话结束摘要。

## 实现与验证入口

命令注册、参数检查和返回文本见
[`xiaotai_console.c`](../../platforms/beken/runtime/src/xiaotai_console.c)；
请求处理见 [`xiaotai_app.c`](../../platforms/beken/runtime/src/xiaotai_app.c)。
两块 BK 板链接同一份 BK 命令实现，GPIO、串口接线及支持的产品功能以各板指南为准。

## 启动异常时使用串口

恢复配置命令在媒体、界面和联网之前开放，不依赖 Wi-Fi 连接、设备绑定、校时、鉴权或平台上线成功。
可使用 `wifi-set`、`wifi-clear`、`tirtc-clear`、`ntp-show/set/reset`、`ep-show/set/reset` 和 `restart`；
具体参数仍按本板命令说明输入。BK 命令须加 `ap_cmd` 前缀，ESP32 直接输入命令。

本地业务未就绪时，`status` 只提示恢复模式，通话、媒体、语音诊断等业务命令返回 `Local runtime not ready`。
修改配置成功后按命令提示重启；写入失败不会报告保存成功或重启。
此入口不能修复串口驱动不可用、系统死机或 Flash 损坏；存储初始化失败时可以查看提示并重启，但保存配置可能失败。

验收应覆盖未配网、未绑定、NTP 不可达、鉴权失败、媒体初始化失败和断网重连；
每种情况下执行查询、修改、重启，并确认配置保留。主机故障注入不能替代当前镜像的实板串口验证。

IPv4 使用不带前导零的十进制格式：接受 `10.0.0.1`，拒绝 `010.0.0.1`。
NTP 响应须来自请求的服务器、匹配本次请求且报告已同步；无效响应继续尝试备用服务器。

`tirtc-clear` 成功后，后台迟到的绑定结果不能在重启前重新写入凭证。
身份存储忙时请重试；重启调度失败时可执行 `restart`，也允许再次手动清除。

`wifi-set` / `wifi-clear` 成功后，重启前的后台配网不能覆盖串口恢复结果。
此时网页保存会提示暂时无法保存，请先重启；存储失败也会提示重试，不会误报为密码格式错误。

ESP 命令注册和参数校验见 [starter_console.c](../../platforms/esp-idf/components/starter_console/src/starter_console.c)。
