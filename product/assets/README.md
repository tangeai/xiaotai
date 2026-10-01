# XiaoTai product assets

This directory owns immutable product assets shared by more than one target.
Board projects select the bundle explicitly; they must not reference another
board project's asset directory.

- `audio/prompts`: 8 kHz, mono, signed 16-bit PCM acknowledgement prompts.
- `audio/rings`: 8 kHz, mono, signed 16-bit PCM call tones.
- `qr/wx_xiaotai.png`: audited source image for the XiaoTai WeChat QR code.
- `models/nihaoxiaotai`: canonical user-provided wake-word model bundles.
- `expressions`: candidate expression themes and their canonical AI emotion
  mapping. Concept sheets are retained for design reuse but are not firmware
  inputs until their manifest marks them runtime-ready.

Targets may retain buildable model copies because generated headers and selected
versions are part of each inference contract, but asset tests must prove those
copies are byte-identical to the selected canonical bundle. The ESP-IDF UI
currently renders its QR code from an embedded matrix; the PNG is retained as
the audited product source asset rather than linked into firmware.
