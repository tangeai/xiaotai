# BK7258 串口命令

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

这些命令需要设备已联网、完成绑定，并且联系人已同步。

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

## 实现与验证入口

命令注册、参数检查和返回文本见
[`xiaotai_console.c`](../../platforms/beken/runtime/src/xiaotai_console.c)；
请求处理见 [`xiaotai_app.c`](../../platforms/beken/runtime/src/xiaotai_app.c)。
两块板链接同一份命令实现，GPIO、串口接线及支持的产品功能以各板指南为准。
