# Generated display assets

`xiaotai_ui_assets.c` is checked in so a normal firmware build has no host font
or image dependency. It contains:

- the fixed official XiaoTai WeChat mini-program QR modules, packed at one bit
  per module; and
- a 16x16 one-bit basic CJK Unicode font for product UI, contact names, and
  live server captions. Its ranges match the ESP32 XiaoTai product font:
  ASCII, common symbols and punctuation, `U+4E00-U+9FFF`, and full-width
  forms.

The home WeChat-call icon uses the shared 28x28 alpha4 resource in
`product/assets/icons/wechat_call/icon_small.h` (392 bytes). Its green background
and border are drawn from shared style definitions. The renderer uses the
existing framebuffer and requires no icon allocation or RGB bitmap copy.

When the official QR or fixed copy changes, regenerate the file explicitly:

```bash
python3 tools/generate_ui_assets.py \
  --qr /path/to/official-mini-program-qr.png \
  --output-dir ap/assets
```

Regenerate shared icon variants from the repository root:

```bash
python3 tools/generate_ui_icons.py
python3 tools/generate_ui_icons.py --check
```

The generator rasterizes the selected Unicode ranges from Noto Sans CJK. Noto Sans CJK
is distributed under the SIL Open Font License 1.1. The generated subset is a
build input, not a runtime dependency on the host font file. The copyright
notice and complete license are retained in [OFL-1.1.txt](OFL-1.1.txt).

Characters outside these ranges (including most text Emoji) render as a visible
fallback box. XiaoTai expression events are rendered as local vector faces and
do not depend on text Emoji. The renderer reads
glyphs directly from Flash with binary search; the complete font is never
copied into internal SRAM or PSRAM.
