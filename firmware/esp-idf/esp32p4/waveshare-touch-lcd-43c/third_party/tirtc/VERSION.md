# TiRTC ESP32-P4 SDK

- SDK: v2.5.0 (embedded build tag: v2.5.0-9088239c)
- Archive: `lib/libTiRTC.a`, supplied SDK package; RISC-V ELF objects verified.
- SHA-256: `9b4e35c7a2cb203fc463417739199429057c8b036936faa11f844c726455301f`
- Headers: `include/tirtc/`; include paths preserve existing `tiRTC.h` and `tirtc/tiRTC.h` consumers.
- ABI: ESP-IDF 5.5.4, 1000 Hz tick, trace/runtime stats disabled, StaticSemaphore_t 84 bytes.
- The package manifest has LWIP_MAX_SOCKETS=10; the application retains 16. This is a runtime socket capacity, not the static semaphore layout.
- Runtime and feature compatibility require device validation after upgrading from 2.3.0.
