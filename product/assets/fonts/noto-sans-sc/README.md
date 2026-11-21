# ESP32 简体中文黑体

[中文](README.md) · [English](README.en.md)

使用 Noto Sans CJK SC Regular 2.004，许可为 [SIL OFL 1.1](OFL.txt)。
[版权声明](NOTICE.txt)保留原始字体的署名；[source.json](source.json)固定下载地址、提交、SHA-256 和转换器版本。
生成的 LVGL 字库随仓库提交，正常构建不需要下载字体或安装转换器。

## 生成

将 `source.json` 中的官方 OTF 下载到 `.local/fonts/NotoSansCJKsc-Regular.otf`，
同时按清单 `symbols.url` 下载 `NotoSansMath-Regular.ttf` 到相同目录，仅补充 `≒`。
两份输入都通过 SHA-256 校验，许可证和版权声明一并保留。
在本地工具目录安装 `lv_font_conv@1.5.3`，然后运行：

```bash
python3 tools/generate_product_font.py --size 16 --font .local/fonts/NotoSansCJKsc-Regular.otf --converter /path/to/lv_font_conv
python3 tools/generate_product_font.py --size 24 --font .local/fonts/NotoSansCJKsc-Regular.otf --converter /path/to/lv_font_conv
python3 tools/generate_product_font.py --size 14 --font .local/fonts/NotoSansCJKsc-Regular.otf --converter /path/to/lv_font_conv
```

脚本核对来源哈希和转换器版本；转换失败不覆盖现有文件。输出只含稳定文件名，不含本机路径。
16px、24px 是 ESP32 当前共用字库，每块板只链接布局选择的一个字号。
生成字体的 16px/24px 行高分别为 22px/34px；固定文字区域须容纳实际行高。
14px 替换 P4 保留的旧字体文件，不在当前产品构建中使用；其 PowerShell 入口调用同一生成器。

## 字符与验证

生成 ASCII、基本汉字 `U+4E00–U+9FFF`、常用标点、箭头、数学符号、全角字符以及 `°℃≒`，
仅收录源字体实际具有的字形。动态 AI 字幕使用基本汉字覆盖，不按固定菜单文案裁剪。
排除未在旧字库出现的日文竖排重复符号 `U+3031/U+3032`，避免它们撑大所有文本行高。

这不是完整 Unicode 或彩色表情字库；未收录的字符不能保证显示，旧 14px 文件的混合表情字形不再保留。

运行 `bash tools/check.sh`，确认产品文案、温度符号和按布局选择字体的检查通过，
再构建立创 S3、M5、P4。实板需核对配网/绑定、联系人、菜单和 AI 长字幕的缺字、换行、裁切与可读性。
构建和主机测试不代表实屏验收。

分发源码及包含字体的固件时须保留 `OFL.txt` 与 `NOTICE.txt`，并在发布说明提供固定提交的许可链接。
转换所得子集沿用 OFL；项目其他源码的许可证不变。旧版本附件不因当前源码替换而自动改变。
