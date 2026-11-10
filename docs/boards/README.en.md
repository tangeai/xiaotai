# Supported Boards

[中文](README.md) · [English](README.en.md)

Check the physical model, choose matching firmware from [Downloads](../downloads.en.md), and follow that board's flashing guide.

This page follows `boards/*/*/board.json`. Board IDs are permanent identities in builds, packages, and logs. Check the full model and PCB revision; firmware for the same chip is not necessarily interchangeable.

AEC means acoustic echo cancellation. Flash is nonvolatile storage; PSRAM provides large runtime buffers for media.

[Firmware downloads](../downloads.en.md) maintains current releases. Board guides identify supported hardware, then explain flashing tools, flashing steps, and first use, followed by split-file flashing, source builds, and hardware parameters.

USB ports, download mode, flash addresses, and image counts are board-specific.

| Board ID | Vendor and model | Platform | Resources | Features | Status | Links |
|---|---|---|---|---|---|---|
| `lckfb-esp32s3` | LCKFB ESP32-S3 (ESP32-S3-WROOM-1-N16R8, PCB V1.0.1) | ESP32-S3 / ESP-IDF 5.5.5 | 16 MB Flash / 8 MB PSRAM | AEC, audio, display, touch, camera, Wi-Fi | Hardware validation recorded | [Firmware and release notes](../downloads.en.md#lckfb-esp32s3) · [Guide](lckfb-esp32s3/README.en.md) |
| `waveshare-esp32p4-touch-lcd-43c-v10` | Waveshare ESP32-P4-WIFI6-Touch-LCD-4.3 | ESP32-P4 / ESP-IDF 5.5.5 | 32 MB Flash / 32 MB PSRAM | AEC, audio, display, touch, camera, Hosted Wi-Fi | Hardware validation recorded | [Firmware and release notes](../downloads.en.md#waveshare-esp32p4-touch-lcd-43c-v10) · [Guide](waveshare-esp32p4-touch-lcd-43c-v10/README.en.md) |
| `lckfb-bk7258` | LCKFB BK7258 | BK7258 / Beken AVDK | 8 MiB Flash / 16 MiB PSRAM / 640 KiB SRAM (build configuration) | Audio, display, touch, H.264 camera, Wi-Fi | Hardware validation recorded | [Firmware and release notes](../downloads.en.md#lckfb-bk7258) · [Guide](lckfb-bk7258/README.en.md) |
| `m5stack-cores3-lite` | M5Stack CoreS3-Lite (K128-Lite) | ESP32-S3 / ESP-IDF 5.5.5 | 16 MB Flash / 8 MB PSRAM | AEC, audio, touchscreen, GC0308, Wi-Fi | Both screen variants have hardware records | [Firmware and release notes](../downloads.en.md#m5stack-cores3-lite) · [Guide](m5stack-cores3-lite/README.en.md) |
| `mgkfb-bk7258` | MGKFB AI Toy Development Board BK7258 (PCB revision unconfirmed) | BK7258 / AVDK SMP | 8 MiB Flash (user-confirmed) / PSRAM capacity pending measurement | Dual eye displays, buttons, audio, H.264 camera, Wi-Fi | Dual eye displays validated on hardware | [Firmware and release notes](../downloads.en.md#mgkfb-bk7258) · [Guide](mgkfb-bk7258/README.en.md) |

See release notes for downloads, feature scope, and known issues, and board guides for flashing, setup, and development. Hardware validation applies only to the recorded firmware and scope, not arbitrary source builds.

M5Stack has historical records for both screens; available variants are still determined by the download page.

When adding a board, update the manifest, short board README, and full guide here. Photos must match the actual hardware revision; do not substitute another revision or display size.

TODO: Complete per-board validation dates, serial-log summaries, feature matrices, PCB and chip revisions, and Hardware IR tied to firmware SHA-256. This fills evidence gaps without changing the fact that the boards have previously run successfully.

All board guides follow [Documentation requirements](../../CONTRIBUTING.en.md#documentation-requirements), covering hardware, toolchain, development, building, flashing, testing, and troubleshooting.
