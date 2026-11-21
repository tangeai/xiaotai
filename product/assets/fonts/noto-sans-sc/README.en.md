# ESP32 Simplified Chinese sans-serif font

[中文](README.md) · [English](README.en.md)

Noto Sans CJK SC Regular 2.004 uses the [SIL OFL 1.1](OFL.txt).
[NOTICE.txt](NOTICE.txt) retains the original copyright. [source.json](source.json) pins the URL, revision, SHA-256 and converter version.
Generated LVGL fonts are committed; normal firmware builds require no font download or converter.

## Generation

Download the official OTF from `source.json` to `.local/fonts/NotoSansCJKsc-Regular.otf`.
Also download `NotoSansMath-Regular.ttf` from `symbols.url` to the same directory, solely for `≒`.
Both inputs are hash-checked; preserve both copyright notices and the OFL.
Install `lv_font_conv@1.5.3` in a local tool directory, then run:

```bash
python3 tools/generate_product_font.py --size 16 --font .local/fonts/NotoSansCJKsc-Regular.otf --converter /path/to/lv_font_conv
python3 tools/generate_product_font.py --size 24 --font .local/fonts/NotoSansCJKsc-Regular.otf --converter /path/to/lv_font_conv
python3 tools/generate_product_font.py --size 14 --font .local/fonts/NotoSansCJKsc-Regular.otf --converter /path/to/lv_font_conv
```

The script checks the source hash and converter version. Failure preserves the existing file.
Output uses stable basenames without host paths. Active ESP32 layouts select one shared 16px or 24px font.
Generated 16px/24px fonts have line heights of 22px/34px; fixed text areas must accommodate them.
The retained P4 14px font is outside the current product build; its PowerShell entry calls the same generator.

## Characters and validation

Requested ranges cover ASCII, basic CJK `U+4E00–U+9FFF`, common punctuation, arrows, math symbols, full-width forms and `°℃≒`.
Only glyphs present in the source font are included. Dynamic AI captions keep basic CJK coverage rather than a fixed-menu subset.

Exclude vertical repeat marks `U+3031/U+3032`, absent from the old font, to avoid inflating all text line heights.

This is not a full Unicode or color emoji font. Unsupported characters are not guaranteed to render.
The old mixed-source emoji glyphs in the retained 14px file are no longer included.

Run `bash tools/check.sh` to check product copy, temperature symbols and layout font selection, then build LCKFB S3, M5 and P4.
On hardware, check onboarding, binding, contacts, menus and long AI captions for missing glyphs, wrapping, clipping and readability.
Host tests and builds do not establish display acceptance.

Retain `OFL.txt` and `NOTICE.txt` with source and firmware distributions and provide pinned license links in release notes.
Generated subsets remain under OFL; other project code keeps its own license. Replacing current source does not change historical release assets.
