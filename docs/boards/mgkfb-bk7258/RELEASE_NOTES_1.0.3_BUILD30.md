# 觅感 AI 玩具开发板 BK7258 v1.0.3 · build.30

日期：2026-11-06。板卡 `mgkfb-bk7258`，变体 `default`，配置 `release`。
用户于 2026-11-06 确认 build.30 已完成实板验证，状态 `HIL_VERIFIED`。
已发布至 [GitHub Release](https://github.com/tangeai/xiaotai/releases/tag/mgkfb-bk7258-v1.0.3-build.30)，下载入口见[固件索引](../../downloads.md#mgkfb-bk7258)。

继承 [build.29 的优化与日志分析](RELEASE_NOTES_1.0.3_BUILD29.md)：移除建连后 300 ms
固定等待、本地结束／切换时可取消旧会话尾音排空、增加各阶段毫秒计时。
build.30 调整诊断分级，保持计时状态及音频队列行为不变。

## 日志分级

| 级别 | 内容 |
|---|---|
| ERROR/WARN | 原有失败、超时、写入错误、队列异常等日志 |
| INFO（默认） | 建连分段、首音频写入时间、每轮结束／未完成汇总、轮末播放队列快照、断开及取消结果 |
| DEBUG | 周期性 AI_AUDIO、AI_UPLINK、AI_SIGNAL，音频 idle、ASR 最终字幕事件、round_start、first_caption |

移除重复的轮开始播放队列快照，保留轮结束和连接关闭快照。
`AI_AUDIO event=first_write` 每次媒体启动仅一次，保留接收及写入时间、耗时和成功标志；
`ok=1` 表示首个处理帧完成写入，不等同物理出声；`ok=0` 也可能是解码失败，
此时尚未调用输出驱动，不能将该条记录视为实际写入。结合解码／写入错误日志判断原因。
被过滤的 DEBUG 在格式化前返回，但轻量计时和计数仍运行，便于动态开启诊断。

串口命令：`ap_cmd log-level app 4` 开启应用 DEBUG；`ap_cmd log-level app 3` 恢复 INFO。
仅调整应用级别，不需要提高 TiRTC 的传输诊断等级。DEBUG 可能包含其他模块的敏感调试内容，
共享日志前需脱敏。默认 AP/CP 为 Beken INFO（3），TiRTC INFO（4）。

## 验证范围

加强已登记的 `test_bk_audio_timing.py`、`test_bk_ai_timing.py`：
旧代码因 INFO 输出详细事件而失败；分级后通过，覆盖 INFO 保留汇总、DEBUG 明细恢复、
过滤前后计数不丢失及公开日志器在格式化前过滤。首音频精简日志同样先失败后通过。
既有停止／切换、旧代次隔离、正常尾音排空及音频队列回归继续执行。

原生 release 编译通过；SDK Kconfig 提示仍在，未发现本次业务代码编译错误。
完整 `bash tools/check.sh`、ASan/UBSan 和 5 板清单通过，ZIP 内文件长度、SHA-256、
BIN/ELF 构建身份校验通过；`git diff --check` 通过。
用户确认本包实板验证通过。确认范围为本轮建连、连续对话及结束／切换使用；
未提供新的量化日志，日志实际降幅、开关日志性能对照、尾音取消的实际静音时间仍未量测。
上一轮 `Task exits abnormally!` 提示仍未定位，本次分级不代表已修复该提示。

## 产物身份及烧录

构建时源码基于 `d5dc104c4f2f0f67dbbf622c3d5f8cc98cfc9875` 加当时尚未提交的工作区改动。
包内身份保留这一构建时状态，后续源码提交不改变已交付镜像。
发布源码提交：`b34c8c1840a25f284b9fcb1c8b3258c5be384af3`。正式包更新验证状态和提交元数据，
BIN 保持不变；公开附件哈希及 BIN 下载回读已校验。
原生产物目录：`firmware/beken/bk7258/mgkfb-bk7258-xiaotai/build/bk7258/mgkfb-bk7258-xiaotai/package/`。

- 包：`mgkfb-bk7258-1.0.3+build.30-validation.zip`。
- 串口镜像：`mgkfb-bk7258-1.0.3+build.30-all-app.bin`。
- 发布 ZIP SHA-256：`568600e1d74c508c7ee5650a824f67aacfdeb54b006d61a54e99ae9bcb9aaab0`。
- BIN SHA-256：`2c7fb16c3715ce7a9eb44cbd8222e817096f286d9c4c5bc778502d5e23167428`。
- BKFIL 起始地址 `0x0`，保留校准及工厂数据，不做全片擦除；本轮不用 `.rbl` 做串口烧录。

后续复测时确认启动版本为 build.30，测试首次建连、连续对话、按键结束和切换通话；
对比 INFO 与应用 DEBUG 日志，并验证正常远端结束没有截断尾音、再次进入 AI 正常。
