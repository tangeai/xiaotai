# TiRTC ESP32-P4 SDK identity

- Source: user-supplied replacement, 2026-11-21.
- Embedded runtime identity: `v2.6.3-22271f54`; supplied package version: `2.6.3` (Mini/KCP).
- Target: ESP32-P4, RISC-V ilp32f.
- Vendor build baseline: ESP-IDF 5.5.4; application baseline: ESP-IDF 5.5.5.
- ABI: 1000 Hz ticks, trace/stats off, StaticSemaphore_t=84, lwIP sockets=10.
- Archive SHA-256: `c2b88d10e4adf812de22051c22fbff3194b7bd8d82e8fa3f1df98be51e41d268`.
- This replacement supersedes `v2.6.0-894972fb`. Prior patch audits
  remain in Git history; they do not prove those fixes are present in this library.
- Hardware verification of the new application/library combination is pending.
