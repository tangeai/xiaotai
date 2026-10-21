# CoreS3-Lite 播放期间音频处理余量排查

核查日期：2026-10-21。当前工程使用 **ESP-IDF v5.5.5、ESP-SR 2.4.7、
esp_audio_codec 2.4.0**。实板基线为 `1.0.1+build.23`，ELF SHA-256：
`a1acc050242e400abd701bfcc64cab6d475ad02ebf221a944a8754a7c94fe45b`。
本文记录调查和下一步验证顺序，不表示性能问题已修复。

## 实测现象

182 秒串口观测中，连续八组播放期间的任务采样显示：

| 观察项 | 播放期间 | AI 已连接但未播放 |
|---|---|---|
| Core 1 空闲样本 | 2.7%～3.3% | 约 40% |
| Core 0 空闲样本 | 26.6%～29.8% | 约 72%～73% |
| Core 1 采集/AEC 任务样本 | 65.2%～67.7% | 约 52% |
| Core 1 播放解码任务样本 | 14.5%～16.5% | 0 |
| AEC 包装函数平均墙钟耗时 | 35.63 ms / 32 ms 输入块 | 17.92 ms / 32 ms 输入块 |

任务百分比是 tick 采样占比，包含采样偏差，不是精确 CPU 计时。
AEC 包装函数计时包含算法、测量、重采样及被抢占/访存等待，不能直接等同于
`aec_process()` 的纯计算耗时。46 个充分覆盖播放的音频窗口，平均用
1189.11 ms 读取/处理 1024 ms 音频；所有观测窗口中看到 5 次采集溢出事件。
窗口不是连续覆盖整个会话，不能把 5 当作会话总数。

稳定嘴型的局部刷新最大 4050 像素，占 320×240 屏幕约 5.3%；字幕、状态等变化
仍有更大刷新。此前关闭嘴型动画的对照未消除采集落后，因此不能仅凭 UI 慢日志
断言是嘴型重绘造成的。麦克风还存在削顶，需与性能问题分开验证。

原始数据和可重跑分析器保存在本地忽略目录
`tests/.local/ui-runtime-20261021/`，包括 `m5-build23-tasks.log`、
`m5-build23-analysis.json` 和 `analyze_m5_tasks.py`。

## 官方指引与当前差异

### 1. 缓存和 PSRAM 工作集

[ESP-IDF 5.5.5 速度优化指南](https://docs.espressif.com/projects/esp-idf/en/v5.5.5/esp32s3/api-guides/performance/speed.html#change-cache-size)
说明增加缓存可能减少缓存未命中，但会减少可用 RAM。
[外部 RAM 指南](https://docs.espressif.com/projects/esp-idf/en/v5.5.5/esp32s3/api-guides/external-ram.html#restrictions)
说明大量外部内存访问可能超过缓存承载范围，退回外部 RAM 访问速度。

当前 M5 是 Quad PSRAM 80 MHz；数据缓存 32 KB、缓存行 32 B。AEC 状态和
计算帧、播放池、多个工作任务的栈位于 PSRAM，同时 UI 也访问 PSRAM。
这些是可核实的竞争条件，但尚没有缓存未命中计数，不能宣称缓存已被证明是根因。

官方 ESP-SR 测试配置使用 64 KB 数据缓存、64 B 缓存行，见固定提交
[sdkconfig.ci.afe](https://github.com/espressif/esp-sr/blob/78a923d29a45e4e1433b909d3da12ab5b0f994fc/test_apps/esp-sr-afe/sdkconfig.ci.afe)。
该配置还使用 Octal PSRAM、不同 IDF 配置并开启运行统计，不能整份复制给本板。

本地 IDF 的 `components/esp_system/port/soc/esp32s3/Kconfig.cache` 和
`components/heap/port/esp32s3/memory_layout.c` 明确：数据缓存从 32 KB
扩大到 64 KB 会移走一块 32768 字节的内部堆。build.23 通话时内部空闲
47299～48415 字节、最大连续块 27648 字节。不能只看 PSRAM 还有 6 MB 就扩大缓存；
连接阶段、DMA 和最大连续块另有要求，也不能把容量差简单当成新固件的实际堆结果。

**优先对照候选：保持 32 KB 容量，仅把缓存行 32 B 改成 64 B。**
IDF Kconfig 允许这一组合，且不会因扩大缓存容量而额外扣掉 32 KB 堆。
这是根据官方配置与本机内存约束作出的工程推断，官方没有保证这个组合能修复本机问题。
缓存行变大对不同访问模式的效果可能不同，必须实测。

### 2. 编译优化

同一份[速度优化指南](https://docs.espressif.com/projects/esp-idf/en/v5.5.5/esp32s3/api-guides/performance/speed.html#improving-overall-speed)
建议评估 `CONFIG_COMPILER_OPTIMIZATION_PERF`（`-O2`）。build.23 的
`build/compile_commands.json` 确认 `starter_aec.c`、`starter_media.c`、
`starter_product.c`、重采样实现均采用 `-Og`。

但 ESP-SR 的 AEC 核心通过预编译 `.a` 库链接，修改工程优化等级不会重新编译它。
`-O2` 能影响源码包装层、UI 和参与构建的支持代码，不能承诺把 AEC 核心加速某个倍数。
将其作为独立对照，不与缓存、绑核调整混在同一版本。

### 3. 双核调度

[IDF FreeRTOS 调度说明](https://docs.espressif.com/projects/esp-idf/en/v5.5.5/esp32s3/api-reference/system/freertos_idf.html#smp-scheduler)
明确每个核心按任务优先级和核亲和性选择就绪任务；未绑核的高优先级任务可以抢占
当前核心。另一核有空闲，并不保证绑核任务自动获得那一核的时间。

当前采集/AEC 固定 Core 1、优先级 7；播放解码任务优先级 8、未绑核。
实测播放任务在 Core 1 占用了约 15%～16% 的 tick 样本，与采集余量下降相符。
仍有约 13%～16% 的 Core 1 样本归在其他任务，未逐一拆分，不能全部归罪于播放任务。
将播放任务移到 Core 0 是有依据的第二类假设，但可能增加 Core 0 的 UI/网络争用，
不能直接认为这是最终配置。

官方还说明未绑核任务使用 FPU 后可能自动绑核，因此应区分创建时配置和运行态。
本轮八组播放采样确实在两个核心都观察到 `board_audio_rx`，未见它只运行于单核；
但没有直接读取 affinity，也不能据此替所有编解码分支作保证。

### 4. AEC 模式与官方性能表的限制

当前使用 FD_LOW_COST、16 kHz、filter_length=4、32 ms 帧，符合
[官方 AEC 文档](https://docs.espressif.com/projects/esp-sr/en/latest/esp32s3/acoustic_echo_cancellation/README.html)
的全双工模式建议。无需为追求速度改成只有线性消除的 SR 模式。

该页面给出 FD_LOW_COST 的 6.28 ms / 32 ms 数据，但页面是 ESP32-S3，测试脚注
却写 ESP32-P4 @ 240 MHz 并带 S3 缓存配置。来源本身存在平台标注不一致，且本机
测量边界不同；不能拿 6.28 ms 当本板的验收线或据此计算慢了多少倍。

## 参考工程采用了什么

用户提供的 `/home/workspace/xiaotai-esp32/m5stack-cores3-se-xiaotai`
当前 `sdkconfig.defaults` 使用 `-O2`、64 KB / 64 B 数据缓存，可作为配置差异线索。
它的媒体实现已使用 AFE feed/fetch、额外采集任务、不同核分配和内存分配策略，
不是与本工程等价的 AEC 性能对照。本轮只参考其配置和任务结构，未移植代码。
官方指南才是上述配置语义的依据；参考工程不能证明当前板实测会改善。

## 单变量验证顺序与判定

1. 固定当前 A-law 8 kHz 上行、Opus 16 kHz 下行、AEC 模式、音量和任务优先级。
   首先单独比较 32 B 与 64 B 数据缓存行，缓存容量保持 32 KB。
2. 根据结果再选择播放任务核分配或 `-O2` 的独立对照；不把几种优化合并后猜原因。
3. 使用相同音源/音量和场景，比较播放前、持续双向音频、停止后的任务分布。
   对照 AEC 耗时分布、音频窗口墙钟/音频时长、采集溢出、下行连续性、UI 响应，
   以及内部空闲/最大连续块。采样阶段只作短时诊断，不常驻 tick hook。
4. 平均耗时低于 32 ms 仍不够：还要检查尾部耗时与持续溢出，并验证连接、挂断、
   重拨和唤醒恢复。增加输入缓冲只能吸收短暂抖动，不能解决持续处理速度不足。
5. 若缓存/调度对照不足以解释问题，再在单独诊断版本拆分 `aec_process()` 与
   包装层计时；不在实时路径逐帧打印，也不扩大为无目标的参数扫描。

本轮只调查和保存资料，未修改固件配置、生成新固件或向设备发送配置命令。
LCKFB S3 也使用相同的 32 KB / 32 B 默认配置，但其硬件与负载不同；M5 的测量
不能直接证明它需要同步修改，P4/BK7258 也不套用本结论。

## 本地资料与验证

官方页面和 IDF 源码快照保存在
`.references/M5Stack_CoreS3_Lite/audio-performance/`，下载时间、来源和 SHA-256
见 `sources-20261021.json`；已有 ESP-SR 固定提交配置见 `sources.json`。
固定 ESP-SR 2.4.7 文档源、官方 voice_communication 示例和 FPU 调度规则的
补充核查见同目录 `OFFICIAL_RESEARCH_20261021.md`。

本文没有行为变更，不增加镜像实现的测试。已有 `test_task_diag.py`、
`test_s3_audio_observation.py`、`test_ui_refresh_observation.py` 覆盖诊断采样与
统计行为；这些主机测试不能证明缓存或调度优化有效。实际配置变更必须另做回归和
匹配固件身份的实板对照。

本轮 `bash tools/check.sh` 通过（含 ASan/UBSan、5 个板卡 manifest 校验；
产品行覆盖率 93.10%、分支覆盖率 64.48%），日志为
`/tmp/m5-official-perf-check.log`。这不替代下一版固件的硬件性能验收。

## 缓存行对照执行方案

后续用户已批准按上述官方指引实施对照。第一阶段仅修改 M5 的数据缓存行，
不修改 LCKFB S3、P4 或 BK7258 的配置。64 B 是待验收候选，不能提前称为性能修复。

| 项目 | A 基线 | B 候选 |
|---|---|---|
| 固件 | build.23，上文 ELF | build.24，见下方身份 |
| 数据缓存容量 / 行 | 32 KB / 32 B | 32 KB / 64 B |
| CPU / PSRAM | 240 MHz / Quad 80 MHz | 相同 |
| 编译优化 | `-Og` | 相同 |
| AI 上下行 | A-law 8 kHz / Opus 16 kHz | 相同 |
| AEC / 任务优先级 / 绑核 | 现有实现 | 相同 |
| 音量 / 麦克风灵敏度 / UI 动画 | 设备现有设置，现场记录 | 不更改 |

### 烧录前检查

1. 保存 A 的应用、ELF、bootloader、分区表和烧录清单，以及完整 sdkconfig、
   依赖锁、编译命令和参与构建源码的哈希，保留回退条件。
2. 运行 `tools/tests/test_cores3_cache_experiment.py`。测试先在旧默认配置上失败，
   再验证修改后的默认值；额外覆盖残留旧 sdkconfig、容量/优化/音频混改和冲突值。
3. 修改 M5 的 defaults 和本地保留 sdkconfig。defaults 不覆盖已保存的配置，因此
   必须在 IDF 配置完成后对最终文件再执行：

   ```bash
   python3 tools/check_cores3_cache_experiment.py \
     --baseline tests/.local/cache-line-20261021/baseline-sdkconfig \
     --candidate firmware/esp-idf/esp32s3/m5stack-cores3-lite/sdkconfig
   ```

   只接受缓存行选择的两个布尔项与派生 `LINE_SIZE` 三个值变化；不屏蔽其他差异。
   核对 `build/config/sdkconfig.h` 中数值为 64，容量仍 32768；源码、依赖与编译优化
   另行校验。构建和完整仓库检查通过才进入硬件阶段。

### 硬件采集顺序

1. **A/B/A 顺序优先**：保留 A 的既有数据用于初筛；严格对照应补采固定场景 A，
   再 B，最后回退 A 复测。旧日志的歌曲内容/音量未完整记录，不能单凭旧 A 与新 B
   的平均值宣布因果关系或双讲效果改善。
2. 每版启动后用 `version`/`status` 核对版本和 ELF 前缀，记录音量、灵敏度、
   Wi-Fi 环境和摆放位置。保留 NVS，不执行全片擦除或改变绑定。
3. 先记录 30 秒 AI 已连接但未播放的状态，再让 AI 以相同音色、相同请求朗读固定
   文本/歌曲约 2 分钟。若服务端生成内容不同，标为场景近似对照，不能称音频逐样本一致。
   核心性能阶段近端保持安静；双讲和触摸测试另设阶段。
4. 使用同一观测脚本交替采集 `status`、5 秒 `task-diag`、`voice-diag 10`。
   task-diag 期间不同时运行 voice-diag，避免叠加诊断开销。按窗口 ID 去重，
   只比较 mode=AI、同一代会话且至少 28/32 块有估计 DMA 播放的窗口。
   每版至少 30 个有效播放窗口，否则结果为数据不足。
5. 播放结束后再记录 30 秒；重复连接/挂断至少 3 次，验证唤醒恢复、触摸退出、
   下行无断续，以及内部空闲/最大连续块没有逐轮下降趋势。网络断开恢复、
   H5/微信单向视频仍需独立回归，AI 场景结果不能替代其他功能验收。

### 判断规则

- 主指标：有效窗口墙钟/音频时长、AEC 包装区间平均耗时、采集溢出事件；
  辅指标：每核空闲采样、UI 局部刷新面积/耗时、下行失败/溢出、内部堆。
- 候选目标：播放窗口平均墙钟/音频比不超过 1.05，AEC 包装平均小于 32 ms，
  观测窗口采集溢出为 0，且下行播放、触摸和连接回归不恶化。
  这些是本项目的实验目标，不是乐鑫保证；平均通过也不能代替尾部延迟和长期稳定性。
- 输出每版样本数和原始指标，既报告收益也报告退化。观测为零不等于未采样期间零故障。
  若 B 没有稳定改善或功能退化，恢复 A，再单独评估调度；不叠加新变量掩盖结果。
- 未完成烧录、启动、播放采集或回退对照的项目必须写“待实板”，不以编译和主机测试
  替代硬件结论。

### build.24 已完成的离线对照

- 默认屏幕：ILI9342E；ESP-IDF v5.5.5；版本 `1.0.1+build.24`。
- 原生应用：`firmware/esp-idf/esp32s3/m5stack-cores3-lite/build/m5stack_cores3_lite_xiaotai.bin`，
  大小 4020912 字节，SHA-256：
  `e0ee432861753a861ff8bed425d136e136a87909820462d53f9273139da1d404`。
- 同目录 ELF SHA-256：
  `468da465d80efa183f2d971804f8c0dacabdfd9c407640a8841215d944a6308f`；
  应用描述符与 ELF 的身份一致，串口应显示前缀 `468da465d`。
- 产物副本保留在该工程 `artifacts/1.0.1+build.24/`，A 的产物也保留，未覆盖。
- 本次全量主机检查、ASan/UBSan、原生编译均通过。构建后检查器确认仅缓存行的
  三个相关配置值改变；生成的 `sdkconfig.h` 为容量 `0x8000`、行长 `64`。
  1682 个参与编译的源码文件和依赖锁与 A 一致，音频/UI 源码没有新增修改。
- `/dev/ttyACM0` 已完成 B 的分文件烧录与启动核验，未清除 NVS。8 MB PSRAM
  内存检查、Wi-Fi/MQTT、唤醒初始化通过；AI 播放已采集，完整声学和功能验收尚未完成。
- 保存目录：`tests/.local/cache-line-20261021/`，含 baseline/candidate 配置、
  源码哈希、失败/通过测试日志、构建日志、候选身份及采集/分析脚本。

### 实板阶段结果

原始数据及分析位于 `tests/.local/cache-line-20261021/`。按窗口 ID 去重，
持续播放取 AI、active=1、DMA 估计播放至少 28/32 块；计时为包装区间墙钟时间。

| 比较项 | A1 / build.23 | B / build.24 | A2 / build.23 回刷 |
|---|---|---|---|
| 持续播放窗口 | 10 | 80 | 25 |
| 平均墙钟 / 1024 ms 音频 | 1220.50 ms | 1111.04 ms | 1199.76 ms |
| AEC 包装区间平均 | 36.96 ms | 32.98 ms | 36.34 ms |
| 所有采样窗口中采集溢出事件 | 2 | 2 | 1 |
| 无播放窗口 AEC 平均 | 17.72 ms | 16.94 ms | 17.82 ms |
| 稳定播放任务采样 Core 1 空闲 | 样本少 | 约 2.7%～3.5% | 约 2.7%～3.0% |

B 的最终下行排空记录为 RX/decoded/written=10855、RX overflow=0；
A2 为 RX/decoded/written=6770、RX overflow=0。两者排空后都恢复唤醒监听。
该计数证明软件路径处理完成，不证明扬声器无断续或双讲合格。
B 两次观测采集溢出分别在持续播放和结束转场窗口，因此前半段“观测为零”不适用于整轮。

**B 未达到项目目标**：墙钟/音频约 1.085，大于 1.05；AEC 平均仍超过 32 ms，
且采样窗口内有采集溢出。A2 恢复较慢的耗时，支持缓存行改善这一假设；但不能将其
当作完整修复或声学效果验收。A1/A2 各自仍少于预定 30 个窗口，不能合并掩盖不足。
云端生成音频不同，音量与灵敏度数值未获确认，百分比只代表本次近似场景。

设备已回刷 build.23（ELF `a1acc0502`），配网与绑定保留。源码 defaults 和原生
build 目录仍是标明待验收的 64 B 候选，不能用它们推断设备当前版本。
当前主机测试与原生构建通过；硬件性能目标失败。完整双讲、触摸、弱网、H5/微信
单向视频仍未在本轮验收。连接/挂断复测记录在本地报告，不代替这些验证。

后续应单独核实播放任务的实际 affinity 和对采集任务的抢占，再决定调度实验。
本轮 tick 采样显示播放线程确实在 core 1 执行，创建优先级 8 高于采集的 7；
这说明存在同核 CPU 竞争，但尚不能把 AEC 的全部额外墙钟时间归因于抢占。
保持当前缓存容量与算法，避免同时改变调度、内部栈和编译优化而无法归因。

回退基线另完成六轮串口 AI 建连/挂断：均进入 active，退出后恢复 waiting 和唤醒。
内部最大连续块固定为 24 KB；挂断后的空闲内存曾下降，停止新连接后回到测试前
水平，因此这次短测未显示持续泄漏。该复测属于 build.23，不能当作 build.24 的
六轮回归结果；触摸、听感和声学双讲仍须另外验证。
