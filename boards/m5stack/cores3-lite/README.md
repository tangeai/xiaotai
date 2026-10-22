# M5Stack CoreS3-Lite

当前工程使用 **ESP-IDF v5.5.5**，目标为 K128-Lite、16 MB Flash / 8 MB **Quad PSRAM**。
完整步骤见[开发板指南](../../../docs/boards/m5stack-cores3-lite/README.md)。
硬件来源、冲突和旧工程参考理由见[资料说明](SOURCES.md)，证据等级见 `hardware-ir.json`。

```bash
. tools/activate_esp_idf.sh
python3 tools/build.py --board m5stack-cores3-lite --variant ili9342e
```

旧屏选择 `ili9342c`。板级代码负责 PMIC/扩展器、GC0308、ES7210/AW88298、LCD 与触摸，
公共产品代码负责配网、绑定、AI、通话和房间。GPIO0 是音频时钟，不是业务按键。
当前正式发布为 ILI9342E 变体的 v1.0.1 · build.42。
验证来源和遗留事项见[验证记录](VALIDATION.md)，不能使用立创板或 CoreS3-SE 的固件与验证结论。
