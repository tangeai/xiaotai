# Beken 公共平台

Beken 与 ESP-IDF 保持独立 SDK 工程。AP/CP 镜像、工具链、分区、RTOS、媒体驱动、
TLS ABI 和 TiRTC 静态库都由 Beken 平台管理。

- `runtime/`：两块 BK7258 板共用的会话、配网、平台协议、TiRTC 生命周期和音视频实现。
- `src/`：公共板级接口适配；包括音频生命周期、无屏呈现和按键采样。
- `third_party/tirtc/`：匹配当前 AVDK SMP 的库、头文件及来源校验信息。
- `tools/`：公共原生构建、版本身份、SDK 补丁；接受所选工程路径，不隐式选择立创板。

`boards/<vendor>/bk7258` 各自管理 GPIO、供电、相机、按键及显示接口。
`firmware/beken/bk7258/lckfb-bk7258-xiaotai` 和 `mgkfb-bk7258-xiaotai` 分别保留
原生 AP/CP 配置、入口、分区与版本计数，通过 `tools/build.py --board <板卡 ID>` 选择。
觅感工程不引用立创原生工程的源码；立创仍使用原显示/触摸实现，觅感选择无屏接口。

通用逻辑从原立创工程迁入本目录，不改变默认立创行为。新板能力限制由其工程显式定义。
主机回归和完整编译只验证软件接入，板级电平、收放/AEC、摄像头和网络业务仍需实板记录。
