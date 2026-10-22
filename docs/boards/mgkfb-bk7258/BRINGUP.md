# 觅感 BK7258 硬件适配说明

使用、烧录和编译步骤见[开发板指南](README.md)，引脚与器件证据见
[Hardware IR](hardware-ir.json)，发布构建及实板验证范围见[验证记录](VALIDATION.md)。
本页只说明当前硬件适配边界，不另行维护功能清单或操作步骤。

## 工程与 SDK

本板使用独立原生工程 `firmware/beken/bk7258/mgkfb-bk7258-xiaotai`，
基于 AVDK SMP（对称多处理）`release/v3.1.1.10`，采用 AP/CP 架构。
工程选择本板的 GPIO、供电、屏幕、相机与音频适配器；公共运行时位于 `platforms/beken`。
SDK、编译器和 TiRTC 库必须满足[来源及二进制接口约束](../../../firmware/beken/bk7258/lckfb-bk7258-xiaotai/SOURCE_PROVENANCE.md)。

Beken AIDK 只用作硬件与驱动参考，不混入其 CPU0/CPU1/CPU2 二进制库、分区或云端业务。
官方参考入口见[Beken 硬件资料](https://docs.bekencorp.com/arminodoc/bk_aidk/bk7258/zh_CN/v2.0.1/hw-reference/index.html)。

## 外设约束

- GPIO52 为共享外设供电，音频停止时不能关闭仍被屏幕或相机使用的供电。
- 两屏各使用独立缓冲；提交后等待两路 DMA（直接内存访问）完成才可重用缓冲。
  一路失败时仍需处理另一已提交传输，不能释放仍被硬件使用的内存。
- 摄像头控制总线和 DVP（并行视频）数据引脚都需正确初始化；只读出传感器 ID 不代表视频已出帧。
  开启 SDK 默认 GPIO 表时，须保留本板 DVP 功能复用。
- 按键适配器只产生产品意图，屏幕只呈现运行时状态；两者不单独发起 HTTP、MQTT 或 TiRTC 连接。
- AEC（回声消除）使用播放参考与麦克风采样，不能把另一开发板的增益、回采时延或焊接信息当成本板实测值。

具体 GPIO、电平和器件来源只在 Hardware IR 维护，修改驱动前先核对对应证据。

## 验证边界

功能确认、原生编译、精确镜像验收及长期压力测试分别记录。
用户已确认本板功能实测通过；对新生成的 BIN，不沿用旧镜像的校验值或实板身份。
物理 PSRAM 容量、Flash JEDEC、PCB 修订、回采电阻和功放时序仍需量测记录。
长期运行、弱网、双讲和双屏传输失败恢复的关闭条件见[问题与需求清单](../ISSUES_AND_REQUIREMENTS.md)。
