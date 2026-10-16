# BK7258 适配状态

## 2026-10-16 补充待机反馈与发布范围

用户随后提供的新一轮日志时间为 13:18:07–14:12:55，设备 uptime 从
480000 增至 3768004 ms，观察跨度约 54 分 48 秒，末尾运行时间为 1 小时
2 分 48 秒。所提供文本中间有截断，不能据此逐条核验整个窗口的连续性。
可见部分心跳 seq=240 至 1884 均返回 rc=0，指标为 READY、23 个任务，
MQTT 发送继续；未见原先的心跳超时、链表断言或重启。

14:09:12 出现一次 `rwnxl_sleep timeout`，同期有 TiRTC DNS 解析及 socket
创建/关闭。此后可见日志继续约 3 分 43 秒；SRAM free 从 103496 降至
103184 字节后保持，PSRAM free 保持 10962360 字节。该单次变化不足以
判定泄漏，也不能单凭日志将 Wi-Fi 警告认定为已修复。

该片段没有启动版本行，不能独立归属 build.49 或 build.50。待机反馈支持
当前修复方向，但不替代 release 配置镜像、音视频会话、断网恢复与长时间
负载验收。线上 1.0.0 更新使用原生 release 配置重新编译，并保留
`BUILD_VERIFIED_HIL_PENDING`；发布变化见 `RELEASE_NOTES.md`。

## build.50 功能测试候选

2026-10-16 在 build.49 待机反馈后复查启动/鉴权/MQTT、H5、AI、设备呼叫、
微信语音、联系人顺序、房间前台/PTT、设置与会话退出路径。
发现关闭准入缺口：SDK 已通知传输断开，但媒体 worker 尚未停止旧音视频时，
`busy()` 已返回 false，新的接入可能抢先进入并被旧清理影响。

修复复用既有关闭状态，不增加轮询或 SDK 生命周期：清空旧句柄前发布屏障，
worker 取走通知后仍保持忙状态；音视频停止及上行状态复位完成后释放屏障，
再通知业务层，允许 AI 转拨与来电接听继续。SDK 回调仍不直接停止媒体。
`test_bk_close_reconnect.py` 在旧实现断言失败，修改后五种模式各 100 轮通过，
并覆盖重复/陈旧关闭、唤醒入队失败重试和清理期间的新接入。

保留 build.49 的专用 SRAM 放置、开发 profile 与心跳诊断。此次未修改 TiRTC
静态库、音视频参数、UI 业务规则或房间分配刷新策略。完整功能测试按
`HARDWARE_ACCEPTANCE.md` 执行；build.49 的待机反馈不能替代 build.50 的实板验收。

## build.48 首次重复入链与 build.49 待机复测

2026-10-16 12:05 实板 build.48 连续失败。首次重复入链的 count=1，
控制任务的事件节点仍在同一事件链表，状态节点仍在延时链表；
tick/wake 分别为 3509/3609、3474/3574、3188/3288。
同一队列 `0x280476f8` 的入口 PRIMASK=0，yield 前及解锁后 PRIMASK=1；
解锁后的 ICSR=0x1000e000（PendSV pending），IPSR/BASEPRI/FAULTMASK=0。
首次异常保存的临界区计数、旧中断状态均为 0；这次是 AP 自己读取后保存，
不能再只归因于 CP 读到了陈旧的计数。随后重复插入形成自环，CP 心跳超时。

build.48 ELF 中控制任务等待前调用的 `xiaotai_tirtc_service_close_wakeup`
对 `s_pending_close_state`（0x28016bc0）执行 LDAEX，比较失败直接返回，
没有 STLEX/CLREX。该变量与内核临界计数（0x28018940）同处普通 SRAM；
SDK spinlock.c 因 SRAM 独占监视器限制，要求自旋锁放在专用 SRAM 区域。
Oct 2 基线没有这个每轮控制任务都执行的关闭事件 CAS，后来 e1de618 引入。
这是与实际冻结时机吻合的候选原因，尚不能凭反汇编证明硬件监视器导致写丢失。

build.49 只把 `s_pending_close_state` 放到 SDK 已有的
`.sram_spinlock_section`，最终 ELF 地址为 0x28003030，保留 CAS、通知、队列、
调度及心跳逻辑。此次只隔离该轮询变量，其他原子变量未一并迁移，避免混入多个变量；
这不构成对所有原子操作或所有会话路径的硬件验证。
`test_bk_room_close_callback.py` 使用目标 GCC 编译实际入口及变量声明，
修改前因变量仍在 `.bss` 失败，修改后确认专用段；既有用例验证通知不重复、
失败重试和事件消费。测试不模拟硬件独占监视器，不等于重启修复通过。

2026-10-16 用户在 build.49 交付后反馈待机日志：UART 时间为
12:19:55.531–12:35:45.544，直接观察窗口约 15 分 50 秒；设备 uptime
从 348000 增至 1298000 ms，末尾已运行 21 分 38 秒。该片段未包含启动版本或
固件哈希，版本关联依据本次交付与反馈上下文，不能由片段独立核验。

- 心跳 seq=174–649 连续，每 2 秒发送且返回 rc=0；未出现心跳超时、assert 或重启。
- 每 10 秒 metrics 均为 READY、23 个任务；内部 SRAM free/minimum 保持
  103168/99736 字节，PSRAM free/minimum 保持 10962360/10954440 字节。
- connects=1、disconnects=0；约每 30 秒出现 MQTT PUBLISH/TX，证明发送路径持续运行，
  不代表已核验服务端接收。RSSI=-128、CPU=-1 和零包计数不作为有效测量结果。

本轮待机复测通过，原先启动数秒后冻结并重启的现象未复现；结果支持此次 CAS
存储位置调整有效，但尚未直接证明独占监视器造成写丢失的底层机制。
继续使用同一 build.49 验证 AI/通话的启动、关闭、再次建连，以及音视频负载、
断网恢复和更长时间稳定性。此次只更新硬件证据，无行为变更；代码回归由上述
既有测试及 build.49 已通过的 tools/check.sh 覆盖。

## build.47 自环证据与 build.48 取证

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


## 2026-10-16 build.46 实板停顿与 build.47 诊断

build.46 已确认启动版本一致，但仍重启。11:15:42 的两次快照均为 tick=5179，
内核锁由 AP core 1 持有；显示线程 TCB=0x60553ad0，控制线程 TCB=0x60557c50。
LR 解析为显示线程 xQueueSemaphoreTake、控制线程 xQueueReceive 和
vTaskPlaceOnEventList。只能定位临界区调用路径，不能将 LR 当作故障当前 PC。
临界区记录深度 1/2 与 nesting 0/0 不一致，仍需辨别写入停顿、数据损坏和采样可见性。

build.47 在原有开发配置下增加内存记录：ap-port phase 1=获取锁、2=CRC 检查、
3=记录 LR/更新 nesting、4=更新 CRC、5=释放锁、6=退出记账、0=操作完成。
ap-event phase 1=获取内核锁、2=插入事件链表、3=更新延时链表、4=释放锁、0=完成。
ap-port 的 owner/count 是获取前记录，不是超时瞬间的锁状态。
ap-event-memory 每个目标只读六个字，地址必须落在本板 SRAM/PSRAM 边界内；不追踪链表。
这些记录由 CP 超时后读取，不在 AP 临界区打印；记录本身会改变少量执行时序。
release 的 CONFIG_CRITICAL_LR_RECORD 关闭后不编入这些记录。

SDK .8 与 .10 的 tasks.c/queue.c 相同，但 spinlock.c/h 的算法、owner 编码和初始化
存在变化，不能用前者相同排除 SDK 回归。尚未确认哪一项造成当前停顿。
主机回归通过不代表板上重启已解决；等待 build.47 对应的首次完整超时快照。

`立创·实战派BK7258` 当前使用独立的 BK-AVDK 3.1.1.10 工程和 TiRTC 2.5.0 组件。
AP（应用处理器）、CP（通信处理器）及合并固件均由同一板卡入口构建。

## 已完成

- SoftAP 配网、验证码绑定、设备登录、MQTT 控制和能力上报；
- H5 设备音视频上行与 H5 到设备的音频下行；
- 16 kHz Opus AI 全双工对话和软件 AEC（声学回声消除）；
- 音频微信 VoIP，音频流编号为 0；
- 设备音频呼叫、联系人、房间对讲和会话互斥；
- GC0308 640×480 H.264 视频，目标 20 fps（每秒 20 帧）；
- ST7789V2 显示、FT6336 触摸和实体按键交互；
- 主机合同测试、构建检查和实板验收脚本。

## 当前产品范围

2026-10-12 用户确认：设备对讲和微信 VoIP 只做音频，视频下行不列为需求或缺陷。
H5 摄像头上行保留。近距离回声暂不处理，不列入当前待办或阻塞条件。

## 仍需补充

- 2026-10-16：build.44 实板仍发生 AP 心跳停止和 CP 超时断言。
  SDK 启动日志错误显示 build.42，已确认是配置缓存，不能据此否认复现。
  build.46 修正身份并增加成对的内核锁/LR 快照；停顿根因和长稳结果仍待实板确认。
- 音频对讲、反复呼叫及业务切换的长稳验收；
- 回应声选择和跨板 UI 对齐，详见[开发板问题与需求清单](../../../../docs/boards/ISSUES_AND_REQUIREMENTS.md)；
- PCB（印刷电路板）照片、Flash（闪存）实测容量和新固件长稳日志待补充。

这些项目按功能开发或证据补充分别跟踪，不阻塞当前源码提交。面向操作者的验证步骤见
[HARDWARE_ACCEPTANCE.md](HARDWARE_ACCEPTANCE.md)。
