# Generated display assets

`xiaotai_ui_assets.c` is checked in so a normal firmware build has no host font
or image dependency. It contains:

- the fixed official XiaoTai WeChat mini-program QR modules, packed at one bit
  per module; and
- a 16x16 one-bit basic CJK Unicode font for product UI, contact names, and
  live server captions. Its ranges match the ESP32 XiaoTai product font:
  ASCII, common symbols and punctuation, `U+4E00-U+9FFF`, and full-width
  forms.

`xiaotai_phone_shortcut.c` is the 44x44 RGB565/alpha conversion of the
canonical `starter_product/assets/phone_shortcut.png`. The generated source
records the input SHA-256 so downstream ports can confirm that they use the
same product asset.

When the official QR or fixed copy changes, regenerate the file explicitly:

```bash
python3 tools/generate_ui_assets.py \
  --qr /path/to/official-mini-program-qr.png \
  --output-dir ap/assets
```

Regenerate the phone shortcut separately with:

```bash
python3 tools/generate_phone_shortcut.py \
  --input /path/to/starter_product/assets/phone_shortcut.png \
  --output-dir ap/assets
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
