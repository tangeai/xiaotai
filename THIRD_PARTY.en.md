# Third-party Pre-release Checklist

[中文](THIRD_PARTY.md) · [English](THIRD_PARTY.en.md)

This file records sources and outstanding actions. It grants no license and is not a complete legal review. The root MIT License covers only project-owned source and documentation, not the assets below.

Items marked as requiring separate permission must be authorized by the rights holder or replaced before public distribution.

| Asset | License status | Source information | Action before release |
| --- | --- | --- | --- |
| TiRTC static libraries and headers in `platforms/**/third_party/tirtc/`, and `firmware/**/components/tirtc_sdk/` | **Separate permission required** | Each SDK package's README/manifest; board packages do not all have the same version, and BK7258 is labeled 2.5.0 | Obtain permission per package for binaries, headers, commercial use, and redistribution; if distribution in a public source repository is not permitted, require authorized users to download separately |
| `platforms/esp-idf/components/starter_voice/vendor/` | **License unconfirmed** | ESP32 preprocessing/postprocessing subset of Voicute onnx-wakeword; exact commit below | Check LICENSE, NOTICE, and attribution for the pinned commit; replace or obtain written permission if no explicit license exists |
| `product/assets/models/nihaoxiaotai/` and target-model copies in firmware projects | **Separate permission required** | User-supplied wake model and associated head data | Identify rights holders for the model, training data, and exports; obtain repository and firmware redistribution rights |
| Retained ESP32-P4 `main/ui/font/lv_font_cn_14.c` | **Not currently cleared for public distribution** | Generation comments reference Windows fonts, including `simhei.ttf` | Replace with a clearly licensed font, such as one under OFL, and run glyph/UI regressions, or obtain explicit redistribution permission; unused source files still require review |
| `product/assets/audio/` and retained prompt/ringtone copies in firmware projects | **Separate permission or proof of original creation required** | Embedded audio resources | Record original projects or asset licenses; replace assets without evidence with original or clearly licensed material |
| Precompiled H.264 libraries and associated components in the ESP32-P4 project | **License and patent terms unconfirmed** | Bundled binaries and headers | Verify pinned versions, sources, LICENSE/NOTICE, patent terms, and binary redistribution conditions |
| IDF managed components | **Subject to each component's license** | Vendor components pinned by `dependencies.lock` | Retain LICENSE/NOTICE and generate a Release dependency list; distribute only when license requirements are met |

The locally referenced Voicute source is commit `61e76c5a12ac33da0def7bceba2e07228ad03e63`. This file does not infer that repository's or asset's license; use the license supplied by the rights holder.

Any item without clear rights or permission blocks public release. Successful builds, traceable sources, or existing repository inclusion do not establish redistribution rights.
