# 小钛 ESP32-S3 架构记录

本文件是贡献者的设计索引：记录已验证的板级事实、参考工程结论、性能边界和
演进计划，避免每次排障重新阅读外部示例工程。构建必需模型在组件内，设计记录在
工程 `docs/`。外部参考工程不参与构建；第三方发布条件见根目录 THIRD_PARTY.md。

当前 UI、runtime、TiRTC 和唤醒实现位于仓库 `platforms/esp-idf/components/`，工程内
对应组件主要负责注册依赖与注入板级资源。板级外设实现位于 `boards/lckfb/esp32s3/`。
旧 UI 与内存方案属于历史记录，修改当前产品前应核对组件的 `CMakeLists.txt`。

## 分层与所有权

| 模块 | 所有权 | UI/实时约束 |
| --- | --- | --- |
| `starter_product` | LCD、触摸、LVGL 页面、产品偏好、背光 | 唯一允许访问 LVGL 对象的模块 |
| `starter_runtime` | AI、设备呼叫、VoIP 会话仲裁和产品快照 | 回调发布快照，不画 UI |
| `starter_media` | I2S、Codec、AEC、音频收发、音量 | 音频 deadline 高于画面效果 |
| `starter_voice` | Voicute TFLite“你好小钛”专用唤醒 | CPU0 最新窗口推理，只投递意图，不切换页面 |
| `platform_client` / `wifi_manager` | 发现、绑定、MQTT、SNTP、AP 配网 | 不得在 LVGL 上下文阻塞 |

状态流固定为：

```text
runtime/media/voice 快照或有界事件
              ↓
 starter_product::product_tick（唯一 LVGL 任务）
              ↓
  持久 UI 对象的文字、可见性和样式更新
```

## 硬件与实时约束

- 板：ESP32-S3 N16R8，ST7789 SPI3 320×240 横屏，FT5x06 触摸，ES7210 +
  ES8311，NS4150B。
- 已验证 LCD：SPI mode 2、PCA9557 控制 CS、`swap_xy=1`、`mirror_x=1`，
  GPIO42 低电平点亮背光。
- ES7210 四槽顺序中 MIC1 在 slot 0；MIC3 是播放参考。I2S0 TX/RX 必须一次
  成对申请并共用时钟，AEC 和上行均消费唯一采集链路。
- CPU1 是采集/AEC/摄像头路径（音频优先级 7/8）。LVGL 固定 CPU0、优先级 6，
  不能为“更流畅”而高于媒体工作。
- 当前保守使用 10 行双 PSRAM 绘制缓冲。历史内部最大连续块曾约 17 KB，未经
  帧耗时、SPI 和碎片数据验证，不扩大内部 DMA 缓冲。

## 参考工程结论

### device-monitor

外部 `tirtc-device-example/complete-applications/esp32-s3/device-monitor`
验证了本板显示路径，并采用显示驱动/UI 分层、前后状态快照和局部更新。小钛
借鉴其对象生命周期和性能观测，不复制其任务优先级，因为两工程媒体布局不同。

### szpi-s3-esp

本机 `.references/立创实战派ESP32-S3参考代码/06-lcd`、`14-handheld`
确认 LCD/触摸方向、背光和
RGB565 资源导入方式。其教学示例中逐帧分配/复制/释放位图的做法不得引入产品。

### xiaozhi-esp32

外部 `xiaozhi-esp32` 仓库 `main/display` 的 `Display` 接口将状态、表情、
聊天文本和省电操作分开，且 UI 只初始化一次。这是小钛的目标形态。它的动态
字形缓存只适用于受控字库；远程字幕可能是任意中文，不能直接照搬，否则会缺字。

## UI 规则与当前进度

1. 同页状态更新不允许 `lv_obj_clean()` 后重建整页；页面切换可暂用保守重建。
2. 每个缓存对象指针必须只由 LVGL 任务访问，页面销毁前设为 `NULL`。
3. 高频路径不得分配 LVGL 对象、图片或字符串；使用预分配对象和有界文本。
4. `PRODUCT_UI_REFRESH_SLOW_US=20 ms` 以上的刷新每 5 秒限频记录一次，先看
   数据再改缓冲或优先级。

已完成：设置页的音量、扬声器、麦克风、休眠按钮保留对象，
`refresh_settings_controls()` 原地刷新；连续 `+/-` 不再整页重绘。通话页也保留
接听、拒绝、闭麦、挂断/取消控件；`refresh_call_controls()` 在来电、建连、通话中
切换文字与可见性，接听动作不重建 LVGL 页面。

后续阶段：

| 阶段 | 改动 | 验收 |
| --- | --- | --- |
| 1 | 引入 `product_ui_snapshot` 与持久状态栏/主页/通话层 | 时钟、RSSI、字幕、来电不重建整页 |
| 2 | 资源化图标和表情、统一主题 token | 截图回归，无缺字/错误图标 |
| 3 | AI、设备呼叫、VoIP、待机唤醒长时 HIL | 2 小时无重启，音频连续，采集 UI P95 |

## 自动验证

```bash
# 先激活自己的 ESP-IDF 5.5.x 环境
idf.py build
bash ../../../../tools/run_host_tests.sh
bash tools/check_product_ui_policy.sh ../../../../platforms/esp-idf/components/starter_product/src/starter_product.c
bash tools/check_memory_placement_policy.sh .
bash tools/check_media_cpu_fairness_policy.sh .
bash tools/check_aec_policy.sh .
bash tools/check_i2s_resource_policy.sh .
```

真机烧录使用 115200，烧录期禁止串口监控；随后单独记录启动、连续设置、唤醒、
AI、来去电、接听与挂断日志。
