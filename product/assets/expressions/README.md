# XiaoTai expression resource pool

This directory preserves candidate expression themes independently of any
board firmware. `manifest.json` is the canonical 21-tag protocol inventory used
by the AI session and local expression browser. The two current concept sheets
cover only nine representative tags and are explicitly marked `partial`; they
are not complete protocol packs.

The checked-in PNGs are concept contact sheets, not runtime sprites. They keep
the complete visual proposal available for later replacement or comparison,
but they include labels and a background and therefore must not be embedded in
firmware as-is. A theme is selectable by a product or board variant only after
all of the following are true:

1. Every emotion has a separately reviewed, label-free frame set.
2. Frame dimensions, anchor points, transparency and animation timing are
   consistent across the theme.
3. The frames are converted to the target's explicit runtime format, such as
   RGB565/RLE for BK7258 or LVGL image descriptors for ESP-IDF.
4. The manifest entry is changed to `runtime_ready: true` and records the
   generated bundle and its checksum.
5. Host asset tests and an on-device visual check pass.

Do not infer a theme from screen size or chip type. A board variant must select
it explicitly. Until a runtime-ready theme is selected, existing procedural
faces remain the product fallback.
