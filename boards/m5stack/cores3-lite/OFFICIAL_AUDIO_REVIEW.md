# CoreS3-Lite 官方音频资料核查

核查日期：2026-10-21。当前固件基线为 ESP-IDF **v5.5.5**、ESP-SR **2.4.7**、esp_codec_dev **1.6.2**，用户实测为 `1.0.1+build.12`。本文区分官方源码事实、用户观察和待验证推断；研究笔记本身不改变生产代码。主线根据下述官方证据，已单独修正 M5 功放增益补偿并建立回归；其他 MIC/AEC 参数不变，尚无声学通过结论。

## 已经查明的差异

### 1. 官方 CoreS3 BSP 有功放增益补偿，build.12 板级初始化缺失

Espressif 官方 CoreS3 `bsp_audio_codec_speaker_init()` 为 AW88298 设置 `.hw_gain.pa_gain = 15`。build.12 的 `board_adapter.c` 中 `aw88298_codec_cfg_t` 只指定 `ctrl_if`、`reset_pin`，其余为零。[固定版本 BSP](https://github.com/espressif/esp-bsp/blob/f259a6abd5808019b1903a422092a4234ca95a51/bsp/m5stack_core_s3/src/bsp_audio.c)

这不是无作用的说明字段。当前依赖的官方驱动通过 `20*log10(codec_dac_voltage/pa_voltage)+pa_gain` 计算补偿，未指定电压时取 3.3 V/5 V；设置音量时将请求 dB 减去此补偿，再转成 AW88298 0x0C 寄存器。[补偿公式](https://github.com/espressif/esp-adf/blob/af7b72fb2b73d4f513d3cede01c13518ab216735/components/esp_codec_dev/esp_codec_dev_vol.c)、[AW88298 音量实现](https://github.com/espressif/esp-adf/blob/af7b72fb2b73d4f513d3cede01c13518ab216735/components/esp_codec_dev/device/aw88298/aw88298.c)

**推断：**同一请求音量，在不触及寄存器上下限的区间，当前配置比官方 BSP 少衰减约 15 dB；端点钳位和 0.5 dB 量化使具体差值依音量而变。它是有明确官方依据的配置差异，可以放大扬声器串入麦克风的信号，但尚不能单凭源码把削顶全部归因于它。这里的 `pa_gain=15` 是音量映射补偿，不是再把实际功放加大 15 dB。M5Unified/ESPHome 使用不同音量实现，不能拿各自百分比直接横向比较。

本地 `replay_aw88298_volume.py` 提取官方 `aw88298_set_vol()` 和音量换算函数，以寄存器写入替身记录结果（输入是 codec dB，不假定 UI 百分比）：

| 请求 codec dB | 原 pa_gain=0：0x0C / 寄存器 dB | 官方 pa_gain=15：0x0C / 寄存器 dB |
| --- | --- | --- |
| -30 | 0x3464 / -26 | 0x5264 / -41 |
| -15 | 0x1664 / -11 | 0x3464 / -26 |
| -10 | 0x0C64 / -6 | 0x2A64 / -21 |
| -5 | 0x0264 / -1 | 0x2064 / -16 |
| 0 | 0x0064 / 0 | 0x1664 / -11 |

复现：`python3 .references/M5Stack_CoreS3_Lite/audio-review/replay_aw88298_volume.py`。这是官方代码的主机数值验证，不是已测出的真实声压差。

### 2. 日志“33 dB”并不等于 ADC 实际配置了 33 dB

esp_codec_dev 1.6.2 的 `get_db()` 将请求 33 dB 转为 `GAIN_30DB`（寄存器低四位 `0xA`）；请求 30 dB 也是 `0xA`。枚举本身存在 `GAIN_33DB=0xB`，因此不是芯片缺少此档位，而是该版本驱动转换存在可复现差异。[官方转换代码](https://github.com/espressif/esp-adf/blob/af7b72fb2b73d4f513d3cede01c13518ab216735/components/esp_codec_dev/device/es7210/es7210.c)、[增益枚举](https://github.com/espressif/esp-adf/blob/af7b72fb2b73d4f513d3cede01c13518ab216735/components/esp_codec_dev/device/es7210/es7210_reg.h)

下载源码与项目 managed component 的 `es7210.c` SHA-256 完全一致：`8c741993683579ffbffd05a227533f4a54d086921de8ed99ff6c12f6574d5921`。本地重放脚本直接提取原函数和枚举，用主机 C 编译器执行，未重新实现换算：

```bash
python3 .references/M5Stack_CoreS3_Lite/audio-review/replay_es7210_gain.py
```

| 请求 dB | 寄存器低四位 | 枚举含义 |
| --- | --- | --- |
| 24 | 0x8 | 24 dB |
| 30 | 0xA | 30 dB |
| 33 | 0xA | 30 dB |
| 34.5 | 0xC | 34.5 dB |
| 36 | 0xD | 36 dB |

用户从灵敏度 3 切换到 4，在该驱动下不会提高实际 PGA。这解释了这两个档位可能“没差别”，但不是播放时无法打断的完整解释。直接修成真正 33 dB 会提高模拟增益，可能进一步削顶，不能把它当作当前 AEC 修复。

## 硬件回采、槽位与时钟

M5Stack 官方 CoreS3 页面 FAQ Q4 明确说明支持本地 AEC，ES7210 MIC3 接扬声器回采，并链接 ESP-ADF AEC 示例。[M5Stack FAQ](https://docs.m5stack.com/zh_CN/core/CoreS3)

现有本地原理图第 3 页用于确认 CoreS3-Lite 板上的实际接法，而不是仅从 CoreS3 名称推断。ES7210 数据手册 Figure 2e 的 TDM I2S 顺序为 Channel 1、3、2、4。官方 esp_codec_dev 选择至少三个 MIC 时写 0x12=0x02 启用 TDM；每通道 gain mask 则按物理 MIC1/MIC2/MIC3/MIC4（bit 0/1/2/3）处理，与线上 TDM 顺序不同。[官方 ES7210 驱动](https://github.com/espressif/esp-adf/blob/af7b72fb2b73d4f513d3cede01c13518ab216735/components/esp_codec_dev/device/es7210/es7210.c)

因此当前实现的静态配置有依据：开启 MIC1/2/3、保留四个线槽，`cores3_audio_unpack()` 取 wire[0] 和 wire[1] 为 MIC1/MIC3；gain mask bit 2 控 MIC3 的 0 dB，没有把 slot1 错当成 gain mask bit1。线槽正确性最终仍需要实际 PCM/寄存器证据；存在非零 reference 和播放时变化支持“有回采”，不证明延迟、相位、饱和和全双工质量均正确。

当前四个 16-bit RX 槽、两个 32-bit TX 槽共用 BCLK=64fs。AW88298 0x06 的 I2SBCK 位需要与这一时钟匹配。M5Unified 普通播放路径使用 32fs，不能整组照搬；当前只读改写 0x30 位为 0x20 的做法保留采样率和格式位。[官方 M5Unified CoreS3 初始化](https://github.com/m5stack/M5Unified/blob/35903583211e1f39ca88920714525fbcd1eb714a/src/M5Unified.inl)

## 哪些官方示例不能证明“播放中可以自然打断”

| 官方来源 | 实际看到的行为 | 可以参考什么／不能证明什么 |
| --- | --- | --- |
| M5CoreS3 `Basic/mic` | 录音前 Speaker.end；回放前 Mic.end；播完再 Mic.begin | 普通录放音、电源/引脚；是轮流录放，不能作为双工 AEC 通过证据 |
| M5Unified CoreS3 Mic 初始化 | 0x4C MIC34_PDN=0xFF，MIC3/4 关闭；只开普通双麦输入 | 普通麦克风初始化；不能覆盖当前 MIC3 回采需求 |
| M5Stack ESPHome CoreS3 配置 | 麦克风 16k，播放 48k；等待 announcement 和 speaker 结束、I2S 总线空闲再启动语音/唤醒 | UI/录放时序；配置中没有 MR AEC 链路，不应照搬其中 36 dB/AGC 作为打断参数 |
| 官方小智、OpenAI Voice Assistant 教程 | M5Burner 烧录及交互说明，未给出可复核的 MIC3/AEC 配置或双讲指标 | 证明存在该产品演示；不能据此声称当前驱动/算法参数已验证 |
| Espressif CoreS3 BSP | 初始化同一 I2S TX/RX、麦克风/功放；普通默认 STD 音频 | 板级功放补偿有价值；没有为本产品证明 MR TDM、削顶余量、实时预算和自然打断 |

来源：[M5CoreS3 mic](https://github.com/m5stack/M5CoreS3/blob/625d80bd8172be492114b8e8b6233b6c2e8974a8/examples/Basic/mic/mic.ino)、[M5Unified](https://github.com/m5stack/M5Unified/blob/35903583211e1f39ca88920714525fbcd1eb714a/src/M5Unified.inl)、[M5Stack ESPHome](https://github.com/m5stack/esphome-yaml/blob/da9c772e53885ffa68023fabd162f4f277a9ab80/common/cores3-satellite-base.yaml)、[小智教程](https://docs.m5stack.com/en/guide/cores3/xiaozhi_voice_assistant)、[OpenAI 教程](https://docs.m5stack.com/en/guide/cores3/openai_voice_assistant)、[CoreS3 BSP](https://github.com/espressif/esp-bsp/blob/f259a6abd5808019b1903a422092a4234ca95a51/bsp/m5stack_core_s3/src/bsp_audio.c)。

M5 FAQ 引用的 ADF 示例确实同时播放和录音，把 AEC 后 WAV 写到 SD 便于比较。它使用 Espressif 音频开发板的兼容表；该表没有 CoreS3 项，不能把链接理解为“已有可直接烧录的 CoreS3-Lite 全套配置”。文档区分 AEC-only 与完整 algorithm，AEC-only 不带 AGC/NS，并提示 SR 双麦配置的 AEC 不应等同 VC。[AEC 示例](https://github.com/espressif/esp-adf/blob/d6e1ef5ccf29ca52dcb8332a3232505366755012/examples/advanced_examples/aec/README.md)、[板型表](https://github.com/espressif/esp-adf/blob/d6e1ef5ccf29ca52dcb8332a3232505366755012/examples/README.md)

## build.12 数据和性能约束

用户提供的播放期窗口 MIC RMS 约 4k–11k、reference 145–384、clean 1k–2.3k，MIC 峰值多次到 32768；会话统计 706 个 512-sample 块中有 18508 个 MIC 样本超过代码的 ±32700 阈值，约 5.1%。这是阈值附近的采样计数，不是“5.1% 音频帧丢失”。无播放时正常听到、播放时自然打断不灵，与双工前端问题一致，但日志未标注每一秒是否近端说话，不能计算真正的双讲保真度或 ERLE。

每块 512 个采样点除以 16 kHz 采样率，对应 32 ms 音频。播放期 `aec-us-avg` 约 45–47 ms，617/706 次超预算，表明当前系统的处理节奏不足；计时包括函数运行及抢占，不能全归因于 AEC 算法本身。`listening=0` 是 AI 会话中停本地唤醒推理，与持续发出 AI 上行音频是不同路径，不能据此认定麦克风关闭。

主线另存的官方 ESP-SR 性能资料在 `.references/M5Stack_CoreS3_Lite/audio-performance/`：固定 ESP-SR commit `78a923d29a45e4e1433b909d3da12ab5b0f994fc` 的 AFE CI 配置为 S3 Octal PSRAM80MHz、I-cache32KB、D-cache64KB/64B；当前 M5 为已实测成立的 Quad80MHz、I-cache16KB、D-cache32KB/32B，LCKFB 为 Octal80MHz、同当前缓存。相同 SoC/算法不能推出相同处理耗时。两项目 `libesp_audio_processor.a` SHA-256 相同为 `670639c3d136c7e3909aa90bcaac573600b7e98b6850be8fd5484b4cdea96dcc`，排除了静态库文件不同这一个变量。

官方 AEC 页面说明 FD_LOW_COST 包含 NLP，AGGR 可能损伤近端语音；当前代码使用直接 `aec_process()` 并不等于漏掉 NLP。页面性能脚注的芯片与所列配置存在容易混淆之处，也不是当前 CoreS3-Lite 整机测试，不能直接承诺达到其表中耗时。[官方 AEC 文档](https://docs.espressif.com/projects/esp-sr/en/latest/esp32s3/acoustic_echo_cancellation/README.html)、[官方 AFE CI 配置](https://github.com/espressif/esp-sr/blob/78a923d29a45e4e1433b909d3da12ab5b0f994fc/test_apps/esp-sr-afe/sdkconfig.ci.afe)

## 据此可执行的下一步及边界

1. 主线已为与官方 BSP 的功放 15 dB 补偿差异建立真实 `audio_codecs_init()` 边界回归（先失败、后修复），并仅在 M5 板级配置补上 `.hw_gain.pa_gain=15.0f`。固定同一输入素材、请求音量和麦克风设置，比较削顶和双讲；不混改 MIC gain/NLP/时钟。
2. 保留真实 Quad PSRAM 模式。将实时性作为独立问题，核查处理阶段的预算和调度；只有有测量依据时才评估缓存或工作缓冲，不能为套用 benchmark 改成不支持的 Octal。
3. 一次受控采样应覆盖远端独说、近端独说和双讲，同步保存 MIC/ref/clean、回采相关性和延迟、削顶计数、AEC/编码耗时及采集丢失。官方麦克风设计指南要求避免削顶，因此加大麦克风灵敏度不是当前默认下一步。[Espressif 麦克风设计指南](https://docs.espressif.com/projects/esp-sr/en/latest/esp32s3/audio_front_end/Espressif_Microphone_Design_Guidelines.html)

以上只是证据支持的验证顺序；没有声称补偿单项即可治愈 AEC，也没有要求用户反复盲试多组参数。后续如改生产代码，必须单独记录变更、回归和硬件结果。

## 本地留档

本次只下载必要文件到 `.references/M5Stack_CoreS3_Lite/audio-review/`，没有克隆整个仓库。`sources.json` 含每个文件 URL、固定 commit、SHA-256、字节数；网页快照没有固定 commit，并注明下载 HTML 未包含动态 FAQ 的限制。官方 FAQ 同时通过在线索引内容核实。`replay_es7210_gain.py` 是本地可重复证据脚本。既有原理图/ES7210 数据手册的来源和哈希见本板 `SOURCES.md` 及 `.references/M5Stack_CoreS3_Lite/sources.json`。
