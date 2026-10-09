# 测试与缺陷回归

修改界面或媒体生命周期前，阅读[二次开发易错点与回归入口](pitfalls.md)。
其中记录生产代码边界、已复现问题和板级验证范围，避免用简化的测试替身掩盖真实缺陷。

行为修改从受影响的公共接口开始写失败用例，再修改实现。公共产品状态机和协议优先放在
`product/tests/`；BK7258 适配边界放在板级 `tools/test_*.c` 或
`tools/test_contract.py`。新增测试必须由根目录 `tools/check.sh` 覆盖。

## 统一入口

从仓库根目录执行：

```bash
bash tools/check.sh
```

只需重复主机测试时，可执行根目录 `bash tools/run_host_tests.sh`。
各 ESP-IDF 工程内的同名脚本只覆盖该工程，不能代替提交前的 `tools/check.sh`。

需要缩短红绿循环时，可先运行受影响的公开测试。例如：

```bash
python3 -m unittest tools.tests.test_product_core.ProductCoreTest.test_ai_protocol -v
python3 firmware/beken/bk7258/lckfb-bk7258-xiaotai/tools/test_contract.py
```

提交前仍须运行全量入口。文档修改不新增只校验文字的测试；应说明原有哪项行为测试或
合同覆盖相关规则。

## 板卡构建

修改 BK7258 固件后，从仓库根目录运行：

```bash
python3 tools/build.py --validate
python3 tools/build.py --board lckfb-bk7258
```

也可在板卡工程目录执行 `bash tools/build.sh`。不要假设工程根目录存在 `build.sh`。
报告最终 `all-app.bin` 路径和 SHA-256；编译成功只能证明构建合同，不能代替实板验证。

## 实板证据

把日志结论绑定到固件哈希，并记录操作、单调运行时间和预期状态。会话问题至少保留：

- 发起意图、HTTP 状态与业务 JSON；
- MQTT 命令/通知类型；
- TiRTC 连接代次、媒体首帧和断开原因；
- 音频上/下行帧数、丢弃数与最终产品状态；
- Fault 寄存器和重启启动段（若发生）。

串口出现线程退出文字不等同于芯片崩溃；只有 Fault、看门狗或重新进入启动段才按重启
处理。HTTP 200 也不等同于业务成功，必须继续检查响应中的业务码。
