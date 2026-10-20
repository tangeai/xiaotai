# CoreS3-Lite ESP-IDF 工程

当前使用 **ESP-IDF v5.5.5**（`b774170ff46c393eeb5e495ea37936038d3f4f4f`）和 TiRTC SDK 2.5.0。
从仓库根目录激活 `. tools/activate_esp_idf.sh`，运行 `idf.py --version` 核对。

```bash
python3 tools/build.py --board m5stack-cores3-lite --variant ili9342e
```

旧屏使用 `--variant ili9342c`。输出在本工程 `build/`；首次使用、烧录、参考来源和
实板待验证项见[开发板指南](../../../../docs/boards/m5stack-cores3-lite/README.md)。
