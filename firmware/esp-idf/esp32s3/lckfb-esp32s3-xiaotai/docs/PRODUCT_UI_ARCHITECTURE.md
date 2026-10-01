# 小钛产品 UI 架构决策记录

状态：进行中（第一阶段已落地）
最后更新：2026-09-04

这份记录是开源贡献者理解屏显、交互和实时约束的入口。它记录已核对过的
参考实现、结论和不采用方案，避免每次修改时重新通读三套工程。

## 1. 当前边界

目标硬件是 ESP32-S3（16 MB Flash / 8 MB PSRAM）、320×240 横向 ST7789 SPI
屏、FT5x06 触摸、ES7210 + ES8311 音频链路。产品 UI 的所有 LVGL 对象只能在
`starter_product` 的 LVGL 任务中创建、访问和销毁；网络、TiRTC 回调、I2S 或
NVS 任务不能直接操作对象。

当前实现入口及职责：

| 位置 | 责任 | 修改原则 |
| --- | --- | --- |
| `components/starter_product/src/starter_product.c` | 板级 LCD/触摸、UI 状态投影、页面和交互 | UI 状态只能由此文件的 LVGL 上下文消费 |
| `components/starter_runtime` | AI、设备呼叫、VoIP 的会话仲裁和产品快照 | 只发布快照/事件，不绘制 |
| `components/starter_media` | Codec、AEC、上行/下行音频及音量 | 音频 deadline 优先于视觉效果 |
| `components/starter_voice` | 唤醒和 MultiNet 命令 | 只能投递有界意图，不切换页面 |
| `tools/check_product_ui_policy.sh` | 产品文案与 UI 结构静态回归 | 每个关键交互约束都应有一条可自动检查的断言 |

## 2. 已核对的参考工程与可复用结论

### device-monitor

参考来源：外部 `tirtc-device-example/complete-applications/esp32-s3/device-monitor`
固定版本；该 checkout 不参与本仓库构建。

- 它把显示驱动和产品 UI 分层，并用状态快照的前后差异做局部更新；页面对象
  是持久的，不把每次状态改变都转换成 `lv_obj_clean()`。
- 它提供 UI 刷新耗时观测、明确的状态栏更新入口和资源管线。小钛应借鉴这些
  边界，而不是复制其线程优先级：该工程的音频线程优先级布局与本工程不同。
- 全/大 PSRAM 绘制缓冲需要量化验证，不能只因参考工程可用就直接替换。

### szpi-s3-esp

本机参考位置：`.references/立创实战派ESP32-S3参考代码/06-lcd` 与
`.references/立创实战派ESP32-S3参考代码/14-handheld`（参考资料不参与构建）。

- 已确认本板 ST7789 的 SPI mode 2、横屏 320×240、`swap_xy=1`、`mirror_x=1`，
  以及 PCA9557 LCD CS 与低电平背光的组合。
- 示例的 RGB565/LVGL 资产方法适合导入图标和表情资源。
- 教学示例中按帧分配、拷贝、释放位图的做法不适合产品；也不应把需要内部 DMA
  的大双缓冲误放入 PSRAM。

### xiaozhi-esp32

参考来源：外部 `xiaozhi-esp32` 仓库的 `main/display`；固定版本见对应来源记录，
不得让构建依赖本机 checkout。

- `Display` 抽象使用稳定的 `SetStatus`、`SetEmotion`、`SetChatMessage`、
  `SetPowerSaveMode` 入口；UI 只初始化一次，随后修改持久对象的文字、可见性或
  图片。
- 顶部状态、内容和底部层固定存在，通知由计时器显示/隐藏。这是小钛第二阶段
  的目标结构。
- 它的动态字形缓存适合受控字库；小钛当前远程字幕可能是任意中文，不能把小智
  的小字库策略直接拿来，否则会产生缺字。固定产品文案可逐步转为子集字体或
  图标资产，远程字幕仍需保留完整覆盖或引入服务端字形协议。

## 3. 实时调度与内存决策

当前音频链路在 CPU1 上运行：采集/AEC 约为优先级 7，回放约为优先级 8，摄像头
也固定 CPU1。UI 因而固定在 CPU0、优先级 6：高于普通控制任务，低于媒体期限，
避免触摸连击和整屏绘制抢占录音/AEC。

显示使用一张 `320×240×RGB565` PSRAM 场景缓冲保持脏区一致性，SPI DMA 每次只
传输 10 行，不占用整屏内部 DMA 缓冲。原因是历史启动日志中内部可用连续块一度
仅约 17 KB；直接切到内部大缓冲会挤压实时分配。后续只在有实际帧时、SPI 传输
耗时和内存碎片数据时调整缓冲。

`PRODUCT_UI_REFRESH_SLOW_US=20 ms` 是当前可观察阈值；超过阈值时每 5 秒最多
打印一次诊断，并分别给出整个 tick 与表情绘制耗时。`.57` 真机在 AI active、
唤醒推理停止时仍反复出现 22–34 ms，排除了 TFLite 推理是直接原因。`.58` 对
未变化的时钟、日期和活动文字停止重复写入；表情只擦除上一帧的眼睛和嘴部区域，
不再为口型变化清空整张 220×108 PSRAM 画布。

## 4. UI 更新模型

```text
runtime/media/voice 的有界快照或事件
                 ↓
        starter_product::product_tick（LVGL 任务）
                 ↓
      页面状态 + 固定对象的局部更新
                 ↓
                  LVGL
```

第一阶段已完成：设置页的音量、扬声器、麦克风、休眠设置保留控件对象，并调用
`refresh_settings_controls()` 原地更新标签。连续点按 `+/-` 不再触发整页
`lv_obj_clean()` 和对象重建。

后续页面改造必须遵守：

1. 页面切换可以创建/销毁过渡页；同一页内的状态变化优先原地更新。
2. UI 回调只修改偏好或调用 `starter_runtime` 的非阻塞入口；不得等待网络或音频。
3. 记录对象指针前必须明确其所有者页面，并在页面销毁前归零。
4. 高频路径不分配字符串、图片或 LVGL 对象；预分配/复用资源。
5. 对外显示的数据先做快照，再一次性应用，避免半帧的联系人、通话或状态栏。

## 5. 演进计划与验收

| 阶段 | 工作 | 验收 |
| --- | --- | --- |
| 0（完成） | UI 固定 CPU0/prio 6，增加分项慢刷新诊断 | 构建策略检查通过；真机启动无黑屏/复位 |
| 1（完成） | 设置页原地更新 | 连续点按音量 20 次，数值和声音同步、无整页闪烁 |
| 1.1（待 HIL） | 周期标签去重、表情局部清除 | `.58` AI active 期间慢刷新显著减少，表情无残影 |
| 2 | 引入 `product_ui_snapshot` 与持久状态栏/主页/通话层 | AI 字幕、RSSI、时钟、来电不重建整屏 |
| 3 | 接入 RGB565 图标/表情资产和统一主题 token | 视觉回归截图；无缺字、无错误图标方向 |
| 4 | 长时 HIL：AI、设备呼叫、VoIP、来去电、休眠唤醒 | 2 小时无重启；音频不中断；记录 P95 UI 耗时 |

建议从仓库根目录执行：

```bash
. tools/activate_esp_idf.sh
python3 tools/build.py --board lckfb-esp32s3
bash firmware/esp-idf/esp32s3/lckfb-esp32s3-xiaotai/tools/check_product_ui_policy.sh \
  platforms/esp-idf/components/starter_product/src/starter_product.c
```

真机烧录应使用稳定串口和 115200 波特率；烧录期间不要同时打开串口监控。完成后
用固定脚本记录启动、触摸、语音、来电、接听、挂断和音量连续操作的日志。

## 6. 明确不做的事情

- 不把 `device-monitor` 的优先级数值原样复制到本工程。
- 不让 SDK/网络回调直接碰 LVGL。
- 不为“性能”无数据地提高 UI 优先级或扩大 DMA 缓冲。
- 不在画面每帧 malloc/free 图像或字符串。
- 不以 UI 失效为理由重启音频、TiRTC 或设备；页面状态须与会话状态解耦。
