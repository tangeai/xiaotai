# Third-party assets

[中文](THIRD_PARTY.md) · [English](THIRD_PARTY.en.md)

This file records binaries distributed with the repository and firmware. The root MIT License covers project-owned source and documentation.

The maintainer confirms that the TiRTC SDK, the `nihaoxiaotai` wake model, and the prompts and ringtones are project-owned and may be redistributed. None of the rows below block public release.

| Asset | Distribution | Source |
| --- | --- | --- |
| TiRTC static libraries and headers in `platforms/**/third_party/tirtc/` and `firmware/**/components/tirtc_sdk/` | Project-owned; redistribution allowed | Each SDK package README and manifest. ESP32-S3, ESP32-P4, and BK7258 packages are 2.6.3 |
| `platforms/esp-idf/components/starter_voice/vendor/` | Distributed with the repository | ESP32 preprocessing and postprocessing subset of Voicute onnx-wakeword. Commit below |
| `product/assets/models/nihaoxiaotai/` and target-model copies in firmware projects | Project-owned; redistribution allowed | Project wake model and associated head data |
| Shared ESP32 `ui_font_cn_16.c`, `ui_font_cn_24.c`, and P4 `main/ui/font/lv_font_cn_14.c` | SIL OFL 1.1 | Noto Sans CJK SC Regular 2.004. Pinned source and hash: [font provenance](product/assets/fonts/noto-sans-sc/README.en.md). Keep that directory's OFL and copyright notice with distributions |
| `product/assets/audio/` and retained prompt and ringtone copies in firmware projects | Project-owned; redistribution allowed | Prompts and ringtones embedded in the project |
| Precompiled H.264 libraries and associated components in the ESP32-P4 project | Distributed under the component license | Binaries and headers carried by the board project. The component directory includes an Apache-2.0 LICENSE |
| IDF managed components | Subject to each component license | Vendor components pinned by `dependencies.lock`. Keep each component's LICENSE and NOTICE |

The locally referenced Voicute source is commit `61e76c5a12ac33da0def7bceba2e07228ad03e63`.
