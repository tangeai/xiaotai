# 觅感 BK7258 原生工程

独立板卡 `mgkfb-bk7258`，使用 AVDK SMP AP/CP。构建、烧录前置检查、按键和实板待验项见
[开发板指南](../../../../docs/boards/mgkfb-bk7258/README.md)。

在仓库根目录执行 `python3 tools/build.py --board mgkfb-bk7258`。
本目录维护本板入口、AP/CP 配置、分区和版本；GPIO 与供电由 `boards/mgkfb/bk7258` 管理，
通用实现从 `platforms/beken` 组合。两块屏幕显示双眼和配网/绑定短文案，
使用实体按键、不启用触摸，保留 H5 摄像头上行。

输出固定为本工程的 `build/bk7258/mgkfb-bk7258-xiaotai/`，不按功能或发布配置增加目录。
用户已确认完成开发板验证。
正式下载、构建身份及剩余专项验证见开发板指南。

串口调试见[BK7258 串口命令](../../../../docs/development/BK7258_SERIAL_COMMANDS.md)，
两块 BK7258 板共用命令实现，常用 CP 串口入口需加 `ap_cmd` 前缀。
