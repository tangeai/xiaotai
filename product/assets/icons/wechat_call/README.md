# Shared WeChat-call icon

The source `icon.svg` defines one rounded chat bubble and receiver cutout.
Both 28x28 and 56x56 variants are rasterized independently from these curves
at 8x supersampling, then quantized to 4-bit alpha coverage. This preserves crisp
interiors and thin antialiased edges without upscaling a small bitmap. The SVG
is a design/build input only; firmware references native-size alpha4 C arrays.

White foreground, green background/border and pressed colors are specified in
`manifest.json`. Each adapter draws the button background, with no RGB bitmap.

| Variant | Native size | Packed bytes | Current selection |
|---|---|---|---|
| small | 28x28 | 392 | S3, BK7258; P4 displays below 480px high |
| big | 56x56 | 1568 | P4 displays at least 480px high |

Both variants use the same vector outline and 16 alpha levels. There is no
runtime vector parser, image decoder, resize or extra framebuffer. Each
compiled consumer includes only the needed variants. PNG files are viewable
previews and are not linked into firmware; `.a4` files and C headers are generated
outputs. Physical hit areas, layout and call intents remain owned by existing
interaction adapters/runtime.

The deterministic standard-library generator supports closed absolute M/L/C/Z
paths with evenodd fill in a 28x28 SVG viewBox. Unsupported commands fail rather
than silently changing the picture. To edit curves, update `icon.svg`, record
its new SHA-256 in `manifest.json`, then regenerate both variants.

From the repository root:

```bash
python3 tools/generate_ui_icons.py
python3 tools/generate_ui_icons.py --check
```

Change canonical artwork/styles here, regenerate variants, and run
`bash tools/check.sh`. The shared-resource tests verify vector source identity,
generated outputs, alpha4 sampling, layout selection and the actual BK
framebuffer render. Hardware still needs normal/pressed color, clipping,
centering, touch-hit and call-intent verification on each selected board.
