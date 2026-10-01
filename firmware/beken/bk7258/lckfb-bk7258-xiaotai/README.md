# 小钛 BK7258 工程

本目录是 `立创·实战派BK7258` 的独立 BK-AVDK 工程。面向使用者的规格、依赖、编译、
烧录和首次使用步骤，请从
[开发板使用说明](../../../../docs/boards/lckfb-bk7258/README.md) 开始。

## 工程边界

BK7258 与 ESP-IDF 工程保持独立，只通过稳定的产品接口复用业务行为。`ap/` 负责应用处理器，
`cp/` 负责通信处理器，`config/` 保存项目配置。板级 GPIO（通用输入输出引脚）、DVP
（数字视频接口）、音频、触摸和电源时序留在 BK7258 适配层。

主要入口如下：

- `ap/src/xiaotai_app.c`：产品会话和媒体资源所有权；
- `ap/src/xiaotai_tirtc.c`：TiRTC 生命周期、连接和媒体路由；
- `ap/src/xiaotai_audio.c`：麦克风、扬声器、A-law、Opus 和 AEC；
- `ap/src/xiaotai_video.c`：GC0308 与 H.264 上行；
- `ap/src/xiaotai_ui.c`：显示、触摸和产品状态；
- `HARDWARE_ACCEPTANCE.md`：实板验收步骤和关键日志。

## 当前能力

项目方已经在实板验证 H5 实时查看与双向对讲、AI 对话和音频微信 VoIP。H5 和微信通话
使用 8 kHz 单声道 A-law；AI 使用 16 kHz 单声道 Opus。AI 在 PCM 阶段使用 AEC、NS
和 AGC；8 kHz 链路使用 AGC，AEC/NS 在完成实机验证前保持关闭。设置页的 1–5 档
麦克风灵敏度调整所有链路共用的 ADC 前端增益，默认增强档，标准档对应 BSP 原值
`0x2d`。详细参数见
[音视频参数与媒体处理](../../../../docs/product/MEDIA_CONTRACT.md)。

## 编译与检查

从仓库根目录执行：

```bash
python3 tools/build.py --validate
python3 tools/build.py --board lckfb-bk7258
bash tools/check.sh
```

普通构建保留 DEBUG 日志，便于开发联调。正式发布时使用
`python3 tools/build.py --board lckfb-bk7258 --release`；该命令会按版本规则递增
`+build.n`，并将 AP、CP 和小钛应用日志统一限制到 INFO，仍保留 INFO、WARN 和 ERROR，
但不输出 DEBUG 载荷。WARN 仅适合故障定位非常有限的特殊固件，不作为默认发布级别。

生成物由构建脚本统一复制到 `output/`。工程内的 `build/`、日志和自动化测试报告不提交。

## 现场日志判读

按单调运行时间和会话代次还原事件，不只看串口接收时间。以下规则来自实板回归，后续
修改会话或触摸流程时必须复查：

- HTTP `200` 只表示请求到达服务端；仍需检查业务 JSON。例如 `40201` 表示目标设备
  不在线，不能归因于 Wi-Fi 或 TiRTC 建连失败。
- TiRTC `-40008` 表示远端关闭。同一代会话已经收到明确结束语义，或 AI 媒体已经
  进入活动态时按正常完成；建连阶段或尚未开始媒体时仍按异常处理。
- 设备通话本机挂断必须先排队本地 TiRTC 断开，再发送平台挂断/取消请求。实机验收需
  确认立即出现“正在结束”、随后返回首页，且 30 秒内无 IPC 心跳超时、断言或重启。
- AI 尾音分成“平台已发送”和“设备已缓存”两段。设备在 `end_session` 后关闭上行、
  先保留 1.5 秒尾音到达窗口，再排空本地缓存，结束流程最多 5 秒；平台仍应先发送
  完整最后一句，再发结束命令并关闭传输。若平台先关闭连接，设备只能排空已经收到的
  音频，无法补播尚未下发的音频。
- `OS:E ... Task exits abnormally!` 单独出现在 TiRTC 线程回收后不构成重启证据。只有随后
  出现 MemFault/UsageFault/HardFault、看门狗或完整启动日志，才按崩溃处理。
- FT6336 的 `read status reg fail`、`touch ID ... out range` 属于触摸总线/采样问题，和
  网络媒体故障分开诊断。危险的单击操作应在按下时只提交一次；PTT 必须保持按下/抬起
  配对。有效按下后发生真实控制器读取失败时，SDK 驱动合成且只合成一次抬起；应用
  空队列不会触发假抬起。实板需确认上行关闭、界面恢复收听且下一次左上返回有效。
- 房间退出的成功证据是 `/v1/call/group/device/leave` 业务成功、媒体关闭且服务端关系刷新
  为已退出；只返回首页或只关闭扬声器不算退出房间。

首页触摸呼叫只选择平台顺序中的首个微信联系人；实体快捷键仍选择 `contacts[0]`。
多人对讲页固定使用矩形“退出房间”和“按住说话”，成员超过单页容量时分页显示。产品
规则以[交互形态](../../../../docs/product/PRODUCT_INTERACTION_PROFILES.md)和
[产品需求](../../../../docs/product/PRODUCT_REQUIREMENTS.md)为准。

## 待办

TODO：补充可公开的 PCB（印刷电路板）版本照片、Flash（闪存）实测容量，以及新固件的完整
实板回归日志。证据补充不改变当前项目方确认的“已验证”状态，也不阻塞源码提交。
