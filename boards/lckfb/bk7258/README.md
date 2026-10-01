# 立创·实战派BK7258

> 板卡照片和准确的 PCB（印刷电路板）版本待补；当前身份必须结合实物丝印和启动探针报告核对。

完整的环境、构建、烧录和验证步骤见
[板卡使用说明](../../../docs/boards/lckfb-bk7258/README.md)。本页只记录板级代码职责。

## 构建与烧录

    python3 tools/build.py --board lckfb-bk7258

使用厂商 BK7258 下载工具将
`firmware/beken/bk7258/lckfb-bk7258-xiaotai/build/bk7258/lckfb-bk7258-xiaotai/package/all-app.bin`
从 `0x00000000` 完整烧录，并保留 RF 校准和工厂数据分区。`app_pack.rbl` 仅用于
OTA（在线升级）。环境准备和工具限制见[工程说明](../../../firmware/beken/bk7258/lckfb-bk7258-xiaotai/README.md)。

构建入口是 `firmware/beken/bk7258/lckfb-bk7258-xiaotai`。Beken 固件保留独立的
应用核、通信核、工具链和分区配置。

本目录保存按键、音频、摄像头、显示和触摸的板级配置。产品模块只处理业务事件、
音视频队列、坐标转换和页面绘制。

当前工程内存图声明 8 MiB Flash、16 MiB PSRAM 和 640 KiB SRAM。启动日志中约
10.6 MiB 的 PSRAM 数值是 AP 动态堆，不是物理容量；Flash JEDEC ID 仍待实板探针
补齐。证据等级和分区明细见[板卡使用说明](../../../docs/boards/lckfb-bk7258/README.md)
及[Flash 预算](../../../firmware/beken/bk7258/lckfb-bk7258-xiaotai/docs/FLASH_BUDGET.md)。

完整固件已经通过构建。硬件行为以当前固件的 HIL（硬件在环测试）结果为准，
不能用旧探针报告替代。
