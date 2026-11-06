# 觅感 AI 玩具开发板 BK7258 验证记录

## 当前发布固件：build.30

2026-11-06 发布 [觅感 AI 玩具开发板 BK7258 v1.0.3 · build.30](https://github.com/tangeai/xiaotai/releases/tag/mgkfb-bk7258-v1.0.3-build.30)。
用户确认该镜像已通过实板验证，状态 `HIL_VERIFIED`；使用统一双屏眼睛固件，default 变体。
移除 AI 建连后 300 ms 等待，本地结束／切换可取消尾音排空；INFO 保留耗时汇总，周期明细降为 DEBUG。
详细变更及验证边界见 [build.30 记录](RELEASE_NOTES_1.0.3_BUILD30.md)。

| 项目 | 记录 |
|---|---|
| 固件 | `1.0.3+build.30`，release |
| 发布源码提交 | `b34c8c1840a25f284b9fcb1c8b3258c5be384af3` |
| 标签 | `mgkfb-bk7258-v1.0.3-build.30` |
| 串口烧录文件 | `mgkfb-bk7258-1.0.3+build.30-all-app.bin`，起始地址 `0x0` |
| BIN SHA-256 | `2c7fb16c3715ce7a9eb44cbd8222e817096f286d9c4c5bc778502d5e23167428` |
| ZIP | `mgkfb-bk7258-1.0.3+build.30-validation.zip` |
| 发布 ZIP SHA-256 | `568600e1d74c508c7ee5650a824f67aacfdeb54b006d61a54e99ae9bcb9aaab0` |

发布复用用户测试的原始 BIN，仅重打包验证状态及源码提交元数据，未重新编译。
512 项主机回归、完整 `tools/check.sh`、ASan/UBSan、5 板清单及原生 release 编译通过。
发布标题、标签、目标提交、三个附件长度／SHA-256 已核对，公开 BIN 下载回读校验通过。
用户使用确认不等于日志开销、实际静音时延、OTA、弱网及长期稳定性专项测量。
`Task exits abnormally!` 提示仍待定位；没有证据将单条提示认定为崩溃。

## 本地待验证包：build.29

2026-11-06 整理 `1.0.3+build.29` release 配置包，状态 `BUILD_VERIFIED_HIL_PENDING`。
本次优化、毫秒耗时统计、镜像校验值与烧录后验证项见
[build.29 变更及日志汇总](RELEASE_NOTES_1.0.3_BUILD29.md)。
完整仓库检查及原生编译通过；最新串口日志未覆盖本地取消尾音排空场景。

## 历史发布记录：build.26

2026-10-24 发布 [觅感 AI 玩具开发板 BK7258 v1.0.3 · build.26](https://github.com/tangeai/xiaotai/releases/tag/mgkfb-bk7258-v1.0.3-build.26)。
修复设备解绑后收到 `HTTP 410 / 6006` 时无限重试，改为保留旧身份并签名重绑。觅感统一为一套包含双屏眼睛显示的固件。

| 项目 | 记录 |
|---|---|
| 固件 | `1.0.3+build.26`，release |
| 源码提交 | `369c53593178279ae83aed12f26ebac6bf750fd4` |
| 验证状态 | `HIL_VERIFIED`，用户确认本轮已测试可用并授权发布 |
| 串口烧录文件 | `mgkfb-bk7258-1.0.3+build.26-all-app.bin`，起始地址 `0x0` |
| 完整 BIN SHA-256 | `50f5633c428f8b80c67234ab8c05b07d3fa9bf43d2c702ed19560ff59952be3c` |
| ZIP | `mgkfb-bk7258-1.0.3+build.26-validation.zip` |
| ZIP SHA-256 | `8a8ccaf1d56eaa79ba4d0c8d4f38b6f95379dce468ec814db4ec656d9cbdbfbd` |

发布沿用用户测试时的 BIN 字节，未重新编译。包内 `build_identity.json` 保留构建时基础提交，
`MANIFEST.json` 记录整理后的源码提交；此次只更新发布验证状态和打包元数据。
产物保留在本工程原生 `build/bk7258/mgkfb-bk7258-xiaotai/package/`。

480 项主机回归、完整 `tools/check.sh`、ASan/UBSan 和 5 板清单检查通过，原生 release 编译通过。
包内 8 个清单文件的长度、SHA-256 和 ZIP 完整性通过；公开下载的 ZIP 校验值与本地一致。
GitHub 标题、标签、附件及源码提交均已核对。用户确认可用不扩展为长期运行、弱网或 OTA 的专项验收。

## 历史验证记录：build.20

以下仅记录旧镜像及其当时的验证状态。

日期：2026-10-23。板卡 `mgkfb-bk7258`。

## 发布镜像

| 项目 | 记录 |
|---|---|
| 固件 | `1.0.3+build.20`，release |
| 构建源码提交 | `ee88e2b56039262d165193a7928e29ede896a307` |
| SDK | AVDK SMP `release/v3.1.1.10`，`398cab28b938e69ba2071a597daf00845dd080b6` |
| 串口烧录文件 | `mgkfb-bk7258-1.0.3+build.20-all-app.bin`，起始地址 `0x0` |
| 完整 BIN SHA-256 | `d60fd424fe225655d4d8578b9f2cbdee29e35d3bb9aee47858de91fd6e0e45aa` |
| OTA SHA-256 | `b83da2d801ff42b6d11592ba9e0bd93093a340b4ce80bb0cc3a2ff77e8244fbf`；OTA 尚未专项验收，暂不使用 |
| ZIP | `mgkfb-bk7258-1.0.3+build.20-validation.zip` |
| ZIP SHA-256 | `0467580d9d8cc1511714578f5ea8c5f84c90ee37db1c41c954999edb13c84654` |

## 功能与变化

双屏眼睛和实体按键、H5 实时查看、AI 对讲及微信音频通话；不支持触摸、设备互呼和多人对讲。

- 保留 v1.0.2 已验证的首个微信联系人快捷呼叫、持续回铃、音频交接和挂断恢复。
- 新增配网启动与 HTTP 探测诊断日志，不改变 DNS、HTTP 响应内容或配网启动顺序。

- 整理烧录步骤、准确文件名、红框截图、文件用途及共用串口命令说明。
- 开发与发布共用各板原生输出目录，板级接线和功能范围保持独立。

## 实板结果与证据边界

此前实板验证覆盖微信呼出、呼入和 AI 转微信：回铃保持到媒体启动，接听后双向声音正常，
挂断后能再次使用。验证日志未再出现播放任务停止超时，通话结束后 PSRAM 恢复。
本轮用户再次确认 BK7258 当前使用正常；iPhone 在觅感上自动弹出配网页已有使用确认。

用户已确认当前 BK7258 使用无问题并要求发布。新版本沿用当前功能代码，更新产品版本后
重新构建；本次两个新镜像没有重新烧录的完整逐项日志。包内保留
`BUILD_VERIFIED_HIL_PENDING`，区分已有功能验证与新文件的精确实板验收。

## 自动检查

- `bash tools/check.sh` 全部通过，包含主机回归、ASan/UBSan 和 6 块板卡清单校验。
- 两块 BK7258 分别完成 release AP/CP 编译及各自契约测试。
- 发布工具逐项校验 ZIP 内 8 个清单文件的长度与 SHA-256，ZIP CRC 检查通过。
- 本版日志只输出固定请求类别、耗时、响应状态和本地发送结果，不输出 Wi-Fi 密码、请求头或正文。

## 剩余专项

OTA、长期运行、弱网恢复、双讲 AEC、音量边界与重启保存、异常情况下的资源回收仍需专项验证。
通话结束后的 `Task exits abnormally!` 告警和部分 METRICS 状态不及时反映业务，仍作为诊断项保留；
不把该单条告警当作崩溃证据。iPhone 自动配网仍需重复连接测试。
硬件 PCB 修订、Flash JEDEC 和物理 PSRAM 容量等证据继续补充。

旧版记录见 [GitHub Releases](https://github.com/tangeai/xiaotai/releases)。
