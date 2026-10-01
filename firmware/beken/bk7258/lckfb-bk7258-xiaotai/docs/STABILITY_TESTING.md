# BK7258 Web 实时查看稳定性测试

## 测试分层

1. 每次提交运行 `python3 tools/test_contract.py` 和完整 AP+CP 构建。
2. 真机冒烟执行 20 次短连接，确认浏览器首帧、音频和设备回到空闲态。
3. 固定随机种子执行 1000 次随机连接；70% 保持 5–30 秒，25% 保持
   1–5 分钟，5% 保持 10–30 分钟；10% 的轮次直接关闭浏览器上下文。
4. 分别执行 24 小时强信号基线和 72 小时弱网/断网恢复测试。

## 浏览器准备

测试机安装仅用于测试的 Playwright：

```bash
python3 -m pip install playwright
python3 -m playwright install chromium
```

先在测试浏览器登录一次并导出 Playwright `storage_state.json`。该文件含登录
状态，不得提交仓库。测试脚本不接受明文密码或 Token 参数。

```bash
python3 tools/web_stability.py \
  --base-url http://127.0.0.1:18011 \
  --device-id DEVICE_ID \
  --storage-state /secure/path/storage_state.json \
  --cycles 1000 --seed 7258
```

固定时长分层测试可直接按设备名称选择目标。例如 100 次测试包含 60 次
1 分钟、30 次 5 分钟和 10 次 10 分钟：

```bash
python3 tools/web_stability.py --base-url https://xiaotai.chat \
  --device-name BK --storage-state /tmp/xiaotai-state.json \
  --dwell-plan 60:60,300:30,600:10 --seed 7258 \
  --output output/bk7258-web-100.jsonl
```

脚本每 10 秒确认连接状态和画布尺寸，按观看时长分别统计成功率与首帧
时间，并生成相邻的 `.summary.json`。登录状态可用
`tools/create_web_test_state.py` 交互生成；账号和密码不会进入命令行、源码或
结果文件。

输出为 `output/bk7258-web-stability.jsonl`。每轮不仅等待连接状态，还要求视频
渲染器把 Canvas 从浏览器默认的 300x150 调整到至少 320x240，以避免“信令已连
但没有首帧”的假通过；随后记录连接耗时、计划停留时间、正常/异常关闭方式及
错误。同步保存设备串口日志，并按固件 SHA-256 建目录。

## 串口日志统一判定

对保存的串口日志运行：

```bash
python3 tools/analyze_runtime_log.py output/device-serial.log \
  --output output/device-serial.summary.json
```

该工具汇总各模式会话启停、非零会话错误、上下行帧、丢帧、AEC 参考
underflow/overflow、连接错误和首尾 READY 资源差值。UsageFault、HardFault、
BusFault、AP crash 等直接判失败。Beken FreeRTOS 的
`Task exits abnormally!` 表示任务入口返回后由 `prvTaskExitError()` 删除，单独
记为生命周期警告，不冒充 CPU 崩溃。

只有建立板级声学基线后才使用可选的 AEC 阈值。例如：

```bash
python3 tools/analyze_runtime_log.py output/device-serial.log \
  --max-ref-under-percent 20
```

不能在没有固定音量、距离和素材时随意设阈值。

## 真机判定

- 不允许 UsageFault、HardFault、watchdog 或非预期重启。
- 每次浏览器退出后，设备必须释放 STREAM、音频和摄像头 owner；下一轮不能
  收到旧 generation 的有效回调。
- 预热十轮后，内部 heap 和 PSRAM 不应呈单调下降趋势。
- `METRICS` 中 internal minimum、CPU、任务栈低水位、RSSI、连接/断线计数
  必须可解析；视频 FPS/丢帧和音频 dropped 由对应媒体日志关联。
- 连接超时、断网和页面异常退出必须恢复到可再次连接状态，不能永久 busy。
- 对 AI/设备呼叫/微信呼叫/多人对讲分别注入一次空句柄连接失败：观察期内不得接受
  第二个 outgoing；资源已恢复时必须继续复用原 TiRTC 实例，资源未恢复时必须完成
  一次运行时重建且设备 uptime 不归零。详细判据见
  [TiRTC 单实例复用与异常恢复方案](TIRTC_RUNTIME_RECOVERY.md)。

最大连续 heap 块在当前 BK7258 SDK 中没有已链接的无副作用查询接口，遥测中
明确输出 `null`。不要用反复试分配大块内存的方式估算，它会改变碎片状态。
