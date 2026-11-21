# BK7258 串口命令

[中文](BK7258_SERIAL_COMMANDS.md) · [English](BK7258_SERIAL_COMMANDS.en.md)

适用于[立创·实战派 BK7258](../boards/lckfb-bk7258/README.md)和
[觅感 AI 玩具开发板 BK7258](../boards/mgkfb-bk7258/README.md)。
两块板共用命令实现，支持的业务由各板固件决定：觅感支持 AI、微信通话和 H5 实时查看，
不支持设备互呼和多人对讲。本文按当前源码整理，已烧录固件的命令以 `ap_cmd help` 为准；
旧发布固件可能缺少后续增加的命令。ESP32 的命令和串口入口不适用本页说明。

## 连接与输入

按对应开发板指南连接串口，设置 **115200 波特率、8 位数据、无校验、1 位停止位**。
烧录完成后关闭下载连接，再打开串口终端；每次输入一条命令，按回车发送。

常用串口连接到 CP（通信核）命令行，小钛应用运行在 AP（应用核）。
应用命令前加 **`ap_cmd` 和一个空格**，例如：

```text
ap_cmd help
ap_cmd status
ap_cmd call-status
```

直接输入 `status` 后出现 `cmd NOT found: status` 时，先检查是否漏了 `ap_cmd`。
`help` 列出 CP 命令，`ap_cmd help` 列出 AP 命令；后者也包含 SDK 命令，本页只说明小钛应用命令。

参数中的 `<...>` 表示需要替换的内容，`[...]` 表示可选参数，实际输入时不带尖括号或方括号。
带空格的名称用英文双引号括起来。返回 `requested` 表示请求已提交，操作是否完成还要看
后续日志及状态；返回 `rejected` 时检查联网、绑定和当前会话。

## 查看状态与日志

| 命令 | 用途 |
|---|---|
| `ap_cmd status` | 查看联网、保存的 Wi-Fi、配网热点、IP、绑定、TiRTC、当前会话和内存 |
| `ap_cmd call-status` | 查看通话类型、呼入/呼出方向、状态、会话代次和超时时刻 |
| `ap_cmd log-level` | 查看当前应用及 TiRTC 日志级别 |
| `ap_cmd log-level app <0..4>` | 调整应用日志级别：0 关闭、1 错误、2 警告、3 信息、4 调试 |
| `ap_cmd log-level tirtc <0..17>` | 调整 TiRTC SDK 日志级别，取值由 SDK 定义，不沿用应用日志的 0..4 含义 |

`status` 中，`owner` 表示当前业务，`pending` 表示待处理来电，`state` 是运行时状态编号。
`internal_free` / `internal_min` 是内部 SRAM 当前剩余量 / 启动以来最低剩余量，
`psram_free` / `psram_min` 是外部 PSRAM 的对应数值，单位均为字节。
`bound=1` 表示本机已保存设备凭证，还需结合平台状态确认服务正常。

通常保留信息级别：

```text
ap_cmd log-level app 3
```

排查时可临时使用 `ap_cmd log-level app 4`。日志级别不保存，重启后恢复固件默认值；
编译时关闭的 SDK 日志不能靠此命令恢复。分享日志前删除密码、令牌等敏感内容。

## 配网、绑定与重启

| 命令 | 用途与结果 |
|---|---|
| `ap_cmd wifi-change` | 断开当前 Wi-Fi，打开配网热点，保留已保存的 Wi-Fi 信息 |
| `ap_cmd wifi-set <SSID> <密码>` | 保存 Wi-Fi 并重启；开放网络的密码填 `""` |
| `ap_cmd wifi-clear` | 清除已保存的 Wi-Fi 并重启，重新进入配网 |
| `ap_cmd tirtc-clear` | 清除本机绑定凭证并重启，重新进入绑定；不清除 Wi-Fi |
| `ap_cmd restart` | 重启设备，保留已保存的设置 |

更换网络前，先结束 AI、查看或通话，再执行 `wifi-change`。手机连接设备热点后，
打开 `http://192.168.6.1` 填写 Wi-Fi 信息。也可直接通过串口保存：

```text
ap_cmd wifi-set "Home WiFi" "your-password"
```

请替换成实际名称和密码；串口终端可能保留输入记录。
`wificlear` 是 `wifi-clear` 的兼容别名，完整输入为 `ap_cmd wificlear`。

## AI 与通话

开始 AI 或发起呼叫前，设备需已联网并完成绑定。呼叫联系人还需完成联系人同步；
接听与挂断命令作用于当前来电或通话。

| 命令 | 用途 | 支持的开发板 |
|---|---|---|
| `ap_cmd ai-start` | 开始 AI 对讲 | 两块板 |
| `ap_cmd ai-stop` | 结束 AI 对讲 | 两块板 |
| `ap_cmd wechat-call ["联系人名称"]` | 呼叫指定微信联系人；省略名称时，选择平台顺序中的首个微信联系人 | 两块板 |
| `ap_cmd call-answer` | 接听当前来电 | 两块板；觅感仅支持微信来电 |
| `ap_cmd wechat-answer` | 只接听微信来电 | 两块板 |
| `ap_cmd call-hangup` | 结束当前通话或取消呼叫 | 两块板 |
| `ap_cmd device-call "联系人名称"` | 呼叫指定设备联系人 | 立创 |
| `ap_cmd device-answer` | 只接听设备来电 | 立创 |

例如，呼叫通讯录中的微信联系人“哥哥”：

```text
ap_cmd wechat-call "哥哥"
ap_cmd call-status
```

## 多人对讲（仅立创）

觅感不支持多人对讲，即使命令出现在帮助列表中，也不能用它开启该功能。

| 命令 | 用途 |
|---|---|
| `ap_cmd room-status` | 查看分配、连接、加入、发言状态及成员数 |
| `ap_cmd room-sync` | 请求刷新平台房间分配 |
| `ap_cmd room-join <6位房间号> [4位密码]` | 请求加入指定房间 |
| `ap_cmd room-talk-start` | 开始发言 |
| `ap_cmd room-talk-stop` | 停止发言，恢复收听 |
| `ap_cmd room-leave` | 退出房间 |

以上为串口维护入口，不增加产品界面的自动刷新触发条件。
`room-talk-start` 与 `room-talk-stop` 应配对使用；开始发言后需要主动发送停止命令。

## 修改平台服务发现地址

普通配网不需要修改这一项。连接自部署平台或指定测试环境时再使用：

| 命令 | 用途与结果 |
|---|---|
| `ap_cmd ep-show` | 查看已保存的服务发现基址；显示 `default` 表示使用固件默认地址 |
| `ap_cmd ep-set <地址>` | 保存新的服务发现基址并重启 |
| `ap_cmd ep-reset` | 删除覆盖项，恢复固件默认服务发现地址并重启 |

例如，使用带 `/pre` 路径的服务发现基址：

```text
ap_cmd ep-show
ap_cmd ep-set http://ep-open.tangeopen.com/pre
```

重启后设备请求 `http://ep-open.tangeopen.com/pre/services`。
地址必须以 `http://` 或 `https://` 开头，可带端口和路径，不接受查询参数（`?`）或片段（`#`）。
TiRTC 连接地址取自服务发现响应的 `tirtc-srv` 字段，`ep-set` 设置的是服务发现入口。
恢复默认地址时执行 `ap_cmd ep-reset`。

## 时间同步服务器

| 命令 | 作用 |
| --- | --- |
| `ap_cmd ntp-show` | 查看主服务器覆盖项；无有效覆盖项时提示可能未保存、读取失败或损坏 |
| `ap_cmd ntp-set time.example.com` | 保存主服务器域名或 IPv4 地址并重启 |
| `ap_cmd ntp-reset` | 清除覆盖项并重启 |

输入最长 253 字符，不带协议前缀、端口、路径或空格，不支持 IPv6。保留固件原有
备用服务器，NTP 使用 UDP/123。成功修改会中断当前会话；保存失败不重启。
恢复出厂设置也会清除该项。重启后须确认校时成功。

默认主服务器为 `pool.ntp.org`，备用为 `ntp.aliyun.com`。`ntp-show` 只查询保存的
配置，不表示校时已经成功。请将 `time.example.com` 替换为可达服务器。局域网示例：

```text
ap_cmd ntp-set 192.168.1.10
# 等待重启后查询
ap_cmd ntp-show
# 恢复默认并重启
ap_cmd ntp-reset
```

## 实现与验证入口

命令注册、参数检查和返回文本见
[`xiaotai_console.c`](../../platforms/beken/runtime/src/xiaotai_console.c)；
请求处理见 [`xiaotai_app.c`](../../platforms/beken/runtime/src/xiaotai_app.c)。
两块板链接同一份命令实现，GPIO、串口接线及支持的产品功能以各板指南为准。

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
身份存储忙时请重试；重启调度失败时可执行 `ap_cmd restart`，也允许再次手动清除。

`wifi-set` / `wifi-clear` 成功后，重启前的后台配网不能覆盖串口恢复结果。
此时网页保存会提示暂时无法保存，请先重启；存储失败也会提示重试，不会误报为密码格式错误。
