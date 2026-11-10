# XiaoTai Documentation

[中文](README.md) · [English](README.en.md)

Start with the [root README](../README.en.md) to understand the product, select a board, download firmware, and flash it. This index covers public user and developer documentation. Board guides define board parameters; the download page defines published versions and assets.

## Usage and downloads

- [Firmware downloads](downloads.en.md): choose a board, screen variant, and current release.
- [Board directory](boards/README.en.md): compare hardware, features, and validation scope.
- [LCKFB ESP32-S3](boards/lckfb-esp32s3/README.en.md)
- [LCKFB BK7258](boards/lckfb-bk7258/README.en.md)
- [Waveshare ESP32-P4-WIFI6-Touch-LCD-4.3](boards/waveshare-esp32p4-touch-lcd-43c-v10/README.en.md)
- [M5Stack CoreS3-Lite](boards/m5stack-cores3-lite/README.en.md)
- [MGKFB AI Toy Development Board BK7258](boards/mgkfb-bk7258/README.en.md)
- [Glossary](glossary.en.md): media, hardware, and development terminology.

Each board guide covers flashing, first use, specifications, media parameters, dependencies, hardware sources, source builds, tests, and troubleshooting. Boards using the same chip do not necessarily share firmware. Check the full model and hardware revision before downloading.

## Development and maintenance

- [Architecture and development](../ARCHITECTURE.en.md): product layers, session and media lifecycles, and directory boundaries.
- [Contributing](../CONTRIBUTING.en.md): adaptation workflow, tests, documentation, and releases.
- [Third-party checklist](../THIRD_PARTY.en.md): dependency sources, permission status, and outstanding actions.
- [BK7258 serial commands](development/BK7258_SERIAL_COMMANDS.en.md): status, Wi-Fi setup, calls, and service addresses.
- [Development pitfalls](development/pitfalls.en.md): lasting implementation constraints and regression entry points.
- [Testing and hardware validation](../tests/README.en.md): host tests, probes, and hardware-in-the-loop validation.

## Documentation scope

Review Chinese facts, steps, and prose before translating public documents into English. The [translation list](../CONTRIBUTING.en.md#public-documentation-and-translation-scope) defines the scope.

Internal product specifications and development, testing, and integration records are not translated or listed in public usage navigation. Developer guides may cite them to establish implementation contracts or validation evidence.

Follow the [retention rules](../CONTRIBUTING.en.md#internal-materials-and-retention): keep temporary records locally, while maintaining specifications, provenance, and validation evidence needed for source maintenance and release traceability in the repository.
