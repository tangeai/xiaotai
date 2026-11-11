# TiRTC ESP32-P4 SDK identity

- Source: user-supplied replacement, 2026-11-11.
- Embedded runtime identity: `v2.6.0-894972fb`; no independent package version supplied.
- Target: ESP32-P4, RISC-V ilp32f.
- Vendor build baseline: ESP-IDF 5.5.4; application baseline: ESP-IDF 5.5.5.
- ABI: 1000 Hz ticks, trace/stats off, StaticSemaphore_t=84, lwIP sockets=10.
- Archive SHA-256: `554295a06173478ba51cb6fc4fc42a09431787377aef7e4ded4ab17165f39cac`.
- This replacement supersedes local batch 20260922-p4-fixes2. Its old patch audits
  remain in Git history; they do not prove those fixes are present in this library.
- Hardware verification of the new application/library combination is pending.
