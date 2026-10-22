# 觅感 BK7258 原生工程

独立板卡 `mgkfb-bk7258`，使用 AVDK SMP AP/CP。构建、烧录前置检查、按键和实板待验项见
[开发板指南](../../../../docs/boards/mgkfb-bk7258/README.md)。

在仓库根目录执行 `python3 tools/build.py --board mgkfb-bk7258`。
本目录维护本板入口、AP/CP 配置、分区和版本；GPIO 与供电由 `boards/mgkfb/bk7258` 管理，
通用实现从 `platforms/beken` 组合。默认 eyes 变体显示双眼和配网/绑定短文案，
`--variant headless` 保留无屏构建；两种变体均使用实体按键、不启用触摸，保留 H5 摄像头上行。

用户已确认双屏版完成开发板验证，发布使用 eyes 变体及 release 配置。
正式下载、构建身份及剩余专项验证见开发板指南。
