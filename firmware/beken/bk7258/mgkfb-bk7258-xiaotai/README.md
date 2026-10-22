# 觅感 BK7258 无屏原生工程

独立板卡 `mgkfb-bk7258`，使用 AVDK SMP AP/CP。构建、烧录前置检查、按键和实板待验项见
[开发板指南](../../../../docs/boards/mgkfb-bk7258/README.md)。

在仓库根目录执行 `python3 tools/build.py --board mgkfb-bk7258`。
本目录维护本板入口、AP/CP 配置、分区和版本；GPIO 与供电由 `boards/mgkfb/bk7258` 管理，
通用实现从 `platforms/beken` 组合。首阶段不初始化屏幕或触摸，保留 H5 摄像头上行。

当前为开发构建，未完成实板验收，未发布 Release。
