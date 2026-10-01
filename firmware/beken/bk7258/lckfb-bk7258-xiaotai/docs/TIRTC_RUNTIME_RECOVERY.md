# TiRTC 单实例复用与异常恢复方案

## 1. 背景和结论

TiRTC 是进程级运行时，不是单次 AI、查看或通话会话。产品正常运行时只执行
一次 `TiRtcInit` 和 `TiRtcStart`，随后由 STREAM、AI、设备呼叫、微信呼叫和
多人对讲共同复用；每个业务会话只负责连接、媒体订阅和断开。

2026-09-28 的 BK7258 旧 archive 实机日志发现一个例外：`TiRtcWhipConnect` 超时并以
`connection == NULL` 回调后，SDK 没有在可观察时间内释放约 27 KiB 内部 SRAM、
约 29 KiB PSRAM 和一个 `rtc_thread`。空句柄使应用无法调用
`TiRtcDisconnect`。继续发起第二次连接会再次占用资源，最终触发低内存保护和
整机软件重启。

本方案仍以单实例复用为主，只把 Stop/Uninit/Init/Start 用作确认发生 SDK
资源滞留后的故障恢复。它不是普通会话生命周期，也不会在每次连接之间执行。

## 2. 修复目标

- 正常会话始终复用同一个 TiRTC 运行时。
- 应用连接期限覆盖 SDK 已观测到的约 11.2 秒异步超时，避免提前作废回调。
- 空句柄失败后禁止下一次连接，先允许 SDK 自行回收。
- 用连接前后的内部 SRAM 差值判断是否真的滞留，不因一次普通失败重建运行时。
- 确认滞留后由产品 Coordinator 非阻塞地重建 TiRTC，不影响 Wi-Fi、MQTT、绑定、
  UI 和设备其他任务。
- 生命周期不能在限定时间收敛时，整机重启仍作为最后保护。

## 3. 状态和模块边界

```text
正常：TiRTC READY
  │
  ├─ 成功/正常断开/失败但资源恢复 ──────────────> 继续复用 READY
  │
  └─ NULL handle
       │ 锁住 outgoing，观察 3 秒
       ├─ retained SRAM <= 12 KiB ─────────────> 解除锁定，继续复用
       └─ retained SRAM > 12 KiB 或 heap < 32 KiB
            │
            v
       STOP -> 等待 SYS_STOPPED -> Uninit -> Init/Start -> 验收 heap
            │                              │              ├─ 恢复 -> READY
            │ 每阶段最多 5 秒              │              └─ 未恢复 -> 重启
            └────────────失败/超时────────────> 整机重启兜底
```

职责划分：

- `xiaotai_tirtc` 适配器记录连接前 heap，持有 outgoing 锁，并判断空句柄失败后
  是否已经自行恢复；它不直接决定产品重启。
- `xiaotai_tirtc_recovery` 是无 RTOS 依赖的纯状态机，只输出 STOP、RESTART、
  COMPLETE 或 REBOOT 动作，可在主机上做确定性单元测试。
- `xiaotai_app` 的 `xiaotai_control` 任务是唯一生命周期 Coordinator，执行状态机
  动作。SDK 回调仍只复制事件或更新原子状态，不在回调栈调用 Stop/Uninit。
- TiRTC 重启所需的设备 ID、Secret、Client ID 和服务入口保存在有界静态缓冲区；
  不写日志、不写测试报告，也不新增 Flash 明文副本。

## 4. 参数依据

| 参数 | 当前值 | 依据 |
|---|---:|---|
| 会话连接期限 | 15 秒 | 实机 SDK 超时回调约 11.2 秒，保留调度余量 |
| SDK 自清理观察期 | 3 秒 | 防止在异步线程退出前误判，同时阻止第二次连接 |
| 允许滞留内部 SRAM | 12 KiB | 明显低于本次约 27 KiB 泄漏，并容忍缓存和调度波动 |
| 发起连接最低 heap | 32 KiB | 保留原有 BK7258 TiRTC 建连安全门槛 |
| Stop/Start 单阶段期限 | 5 秒 | 防止恢复状态永久卡住 |

这些值是当前 BK7258、TiRTC 2.5.0 和固件资源布局的产品参数，不应直接复制到
其他芯片或 SDK 版本。后续取得 `largest free block` 能力后，应把连续块也纳入
准入和恢复判断。

## 5. 失败语义

- `TiRtcWhipConnect` 返回 0 只表示异步提交成功，不代表连接建立。
- 回调有有效 handle 时，失败连接必须走 `TiRtcDisconnect` 并等待
  `on_disconnected`，不能重建运行时替代正常 drain。
- 回调无 handle 时，应用没有合法的连接级清理接口；只有确认 SDK 没自行恢复后，
  才升级为进程级 TiRTC 恢复。
- 恢复期间 `xiaotai_tirtc_busy()` 保持为真，按键、触摸和业务命令不能发起新的
  outgoing session。
- 正在恢复时不重启网络和 MQTT。平台在线通道继续运行；收到 `SYS_STARTED` 后
  还必须比较连接前后的内部 heap；验收通过后才恢复联系人刷新和空闲 UI。
- `SYS_STARTED` 只证明新运行时启动成功，不证明旧运行时的内存已经释放。若内部
  SRAM 仍滞留超过 12 KiB，必须执行整机重启，不能把状态误报成 READY。

## 6. 验证方案

### 已自动验证

- `python3 tools/test_contract.py`
  - 覆盖正常空闲、不恢复；
  - 覆盖 STOP → STOPPED → RESTART → READY；
  - 覆盖 32 位单调时钟回绕；
  - 覆盖 Stop 阶段超时转 REBOOT；
  - 锁定 15 秒连接期限和 Coordinator 接线。
- `./tools/build.sh`
  - AP、CP 和组合包构建通过；
  - 本次 `all-app.bin` SHA-256：
    `06a84150fd1abd68c1fb15ecf6c220bb980ff7fabda6848b96620e2e71b26aaf`。

### 必须补做的实机 HIL

主机测试不能证明闭源 TiRTC 库会在 `TiRtcUninit` 后释放实际线程和堆，因此发布
前必须用上述精确固件执行：

1. 人为制造 WHIP 超时，确认只允许一次 outgoing，观察期内第二次按键返回 busy。
2. 若 SDK 自行恢复，日志出现 `null-handle cleanup recovered`，不得出现 Stop、
   Uninit 或设备重启。
3. 若确认滞留，日志必须依次出现 cleanup leaked、SDK stopped、runtime restart
   submitted、SDK ready、runtime recovery complete，且启动计时不归零。
4. 恢复后立即执行一次远程查看和一次 AI 对讲，确认 MQTT 在线和联系人仍可用。
5. 连续制造 10 次失败；每轮恢复后任务数和 internal heap 不得单调下降，不得出现
   UsageFault、HardFault、watchdog 或永久 busy。
6. 如果 `TiRtcStop`/`TiRtcUninit` 后仍无法恢复 heap 或任务数，应保留整机重启兜底，
   并把日志和固件哈希提交给 TiRTC SDK 维护方处理库内资源泄漏。

### 2026-09-28 实机反馈

第一次恢复状态机验证通过了 Stop、`SYS_STOPPED`、Uninit、重新 Start 和 MQTT 保持
在线，但资源验收失败：内部 heap 从连接前 113,136 字节降到 85,024 字节，重新
Start 后为 85,056 字节，只恢复了 32 字节；PSRAM 也没有恢复。日志中的多个
`Task exits abnormally!` 出现在 SDK Stop 清理线程期间，随后虽然收到
`SYS_STOPPED` 和 `SYS_STARTED`，仍不能把它当作资源回收成功。

因此实现增加了重启后的 heap 验收。对当时的 TiRTC 2.5.0 库，这条路径会在第一次
确认泄漏后尝试一次受控运行时恢复；若仍是上述结果，会立即整机重启，而不是回到
READY 后继续消耗第二份约 28 KiB 内部 SRAM。根治仍需要 TiRTC SDK 修复空句柄失败
路径的资源释放。2026-09-29 换入的 BK7258 mini archive 必须重新板测，不能直接
继承这份旧 archive 的泄漏结论。

## 7. 后续重构方向

当前修复把恢复策略从 `xiaotai_app.c` 中抽成了可测试的深模块，但配置保存和动作
执行仍由 Coordinator 管理。后续只有在出现第二种 SDK 运行时故障时，才考虑把
“持久配置 + 生命周期动作执行”继续下沉到独立 supervisor；不要为形式上的文件
拆分增加线程、第二套状态机或第二个 TiRTC 实例。
