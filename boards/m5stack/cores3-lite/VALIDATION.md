# CoreS3-Lite ILI9342C 验证记录

本项目仅支持 ILI9342C。维护者于 2026-11-21 确认硬件范围后，移除其他面板配置及其验证记录；
旧记录保留在 Git 历史，不改写为 ILI9342C 的实板证据。

## 当前公开测试镜像

- 内嵌版本：`1.1.0+build.91`；统一发行：`v1.2.0-beta.1`。
- 完整 merged BIN SHA-256：`702e1166bab0f7a5ab85a6af144d9b158a87c859278b40fcb13df7c00c556c95`。
- 应用 BIN SHA-256：`324c697ae2326f97301f548a7c4c519d22300fdaeba1c08dda65cb89651a8eb2`。
- 状态：原生构建及主机检查通过，`BUILD_VERIFIED_HIL_PENDING`。
- [下载与双语说明](https://github.com/tangeai/xiaotai/releases/tag/v1.2.0-beta.1)。

## 仅保留 C 面板的代码验证

默认构建和显式 `--variant ili9342c` 使用同一配置。移除的变体必须拒绝，不回退到默认面板。
CLI 默认选择和面板命令表回归在修复前失败；修复后通过。命令表检查 C 的解锁字节与伽马指令。
构建分发显式传递唯一的命名变体，避免旧 CMake 缓存选择其他屏幕。

`tools/build.py --board m5stack-cores3-lite --release` 原生构建通过，身份为 `1.1.0+build.92`。
BIN/ELF 与哈希见同目录 `hardware-ir.json`。这是代码移除后的构建验证，不覆盖线上 build.91。

## 待实板

当前环境没有可用串口，未执行烧录。显示颜色/方向、四角触摸、音频、摄像头、配网绑定、
AI/H5/呼叫、弱网和长时间运行均需绑定精确镜像验证；不沿用历史构建的通过结论。
