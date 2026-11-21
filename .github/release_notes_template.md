<!--
Copy this bilingual template for each unified release. Fill every {{placeholder}}.
Follow docs/development/release-policy.md for public names and the board download map.
Within each section, distinguish changes and validation for every board/variant.
Follow CONTRIBUTING.md: review actual changes since a stated baseline, limit claims
 to this board/variant, and keep Chinese and English facts identical.
Use “无 / None” for an empty change category. Do not copy examples as facts.
This file is a project convention, not an automatically applied GitHub feature.
-->

[中文](#中文) · [English](#english)

## 中文

### 版本概览

{{一句话说明本次版本的主要变化和适用用户。}}

| 项目 | 内容 |
| --- | --- |
| 开发板 / 变体 | {{完整板名、board ID、硬件/屏幕变体}} |
| 版本 / 类型 | {{统一发行版本}} · {{正式发布 / pre-release}} |
| 比较基线 | {{同板同变体的上一版本或明确的源码提交；缺失时如实说明}} |

| 开发板（中文） | Board (English) | 开发板实物图 | BIN 下载链接 | 内嵌固件身份 |
| --- | --- | --- | --- | --- |
| {{中文型号}} | {{English model}} | {{主页对应开发板图片}} | {{实际 BIN 链接}} | {{实际产品版本与 build}} |

### 新增

- {{新能力、适用场景及用户现在能做什么；没有则写“无”。}}

### 调整

- {{原有行为如何变化、调整后的行为及升级影响；没有则写“无”。}}

### 修复

- {{触发条件 → 原来的问题 → 本版处理结果；没有则写“无”。}}

### 已知问题与遗留事项

- {{仍未解决的问题、受影响场景、可用规避方法和后续验证要求；无新增问题也要保留仍适用的旧问题。}}

### 验证情况

- 自动化：{{实际执行的构建、测试及结果；不将编译通过视为功能验收。}}
- 实板：{{精确镜像、板卡/变体、测试来源、场景和结果；用户反馈与自动化/HIL 测量分开。}}
- 待验证：{{未覆盖的场景、弱网/AEC/长时间运行等；没有证据不能填“通过”。}}

### 下载与升级

- 附件：{{每个板卡/变体一个 BIN；使用实际资产名称与 SHA-256，无固件 ZIP。}}
- 升级影响：{{烧录地址、配置是否保留、分区/硬件兼容性、必要的迁移步骤。}}
- 来源：{{源码提交与验证记录链接；如实区分原始构建身份和打包提交。}}

## English

### Overview

{{One sentence describing the main change and intended users.}}

| Item | Details |
| --- | --- |
| Board / variant | {{Full board name, board ID, hardware/display variant}} |
| Version / channel | {{Unified distribution version}} · {{Stable / pre-release}} |
| Comparison baseline | {{Previous version for the same board/variant or explicit source commit; disclose if unavailable}} |

| 开发板（中文） | Board (English) | Board photo | BIN download link | Embedded firmware identity |
| --- | --- | --- | --- | --- |
| {{Chinese model}} | {{English model}} | {{Matching board image from the homepage}} | {{Actual BIN link}} | {{Actual product version and build}} |

### Added

- {{New capability, applicable use case, and what users can now do; write “None” if empty.}}

### Changed

- {{Previous behavior, new behavior, and upgrade impact; write “None” if empty.}}

### Fixed

- {{Trigger → previous failure → behavior in this version; write “None” if empty.}}

### Known issues and outstanding work

- {{Unresolved issue, affected scenario, available workaround, and remaining validation; retain applicable earlier issues.}}

### Validation

- Automated: {{Builds and tests actually run, with results; compilation alone is not feature acceptance.}}
- Hardware: {{Exact image, board/variant, evidence source, scenarios, and results; distinguish user feedback from measured HIL.}}
- Pending: {{Unverified scenarios, weak-network/AEC/long-run checks; never claim success without evidence.}}

### Downloads and upgrade

- Assets: {{One BIN per board/variant; give actual asset names and SHA-256, with no firmware ZIP.}}
- Upgrade impact: {{Flash addresses, configuration retention, partition/hardware compatibility, and required migration.}}
- Provenance: {{Source commit and validation-record link; distinguish original build identity from the packaging commit.}}
