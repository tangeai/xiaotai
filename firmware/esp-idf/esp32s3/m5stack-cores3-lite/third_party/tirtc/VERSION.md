# TiRTC ESP32-S3 SDK

- Version: v2.5.0 (embedded build tag: v2.5.0-9088239c)
- Archive: `lib/libTiRTC.a`; SHA-256: `7334e846ed4261b5297607c85acafe1d586a22374f7c8e37b102b2742e47c7aa`.
- Target: Xtensa ESP32-S3, ESP-IDF 5.5.4, esp-14.2.0_20260121.
- ABI: 1000 Hz ticks, trace/runtime stats disabled, StaticSemaphore_t 84 bytes.
- Source: user-provided mini SDK package, copied unchanged into third_party/tirtc.
- Project CMake and compatibility code remain in components/tirtc_sdk.
- Compilation does not replace on-device call, AEC and reconnect validation.
