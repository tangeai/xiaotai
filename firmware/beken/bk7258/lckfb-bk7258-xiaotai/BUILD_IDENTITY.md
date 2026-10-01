# Build identity

| Item | Locked value |
| --- | --- |
| Product | XiaoTai for LCKFB BK7258 |
| Board | Default LCKFB BK7258 development board configuration |
| Platform SDK | Official Gitee BK-AVDK SMP `release/v3.1.1.8` |
| Platform SDK commit | `1cfd56af09a3cb6470f35f1e0c604035ed1b6ee7` |
| TiRTC | TiRTC-Nano v2.5.0 mini |
| TiRTC package | `tirtc__beken-bk7258__gcc-arm-none-eabi-10.3-2021.10__v2.5.0__mini` |
| TiRTC target | `beken-bk7258`, built for BK-AVDK `3.1.1.8-official-1cfd56af` |
| TiRTC archive SHA-256 | `47e26827ca2419084163e3656dca4d6e508e433381784e9c03f232d3dbaa4171` |
| TiRTC crypto provider | Official BK SDK `psa_mbedtls` component, Mbed TLS 3.5.2; no bundled/project-vendored Mbed TLS |
| TiRTC HTTP SSL | Disabled by the package (`SSL=disabled`) |
| TiRTC media DTLS | Disabled by the package (`DTLS=disabled`); current sessions also use `use_dtls=0` |
| AP platform transport | Discovery at `http://ep-open.tangeopen.com/services`; this open deployment returns `http://` REST/TiRTC and `mqtt://` MQTT endpoints |
| lwIP active TCP PCB limit | 12 (project patch over the official SDK default of 8) |
| Camera video profile | GC0308 VGA, 20 MHz MCLK, H.264 20 fps, quality level 2, 30-frame GOP |
| Toolchain | GCC Arm None EABI 10.3.1 |
| ABI | Cortex-M33, FPv5-SP-D16, hard-float |
| Images | BK7258 AP + CP + bootloader package |
| Flash | 8 MiB configured address space; runtime JEDEC identification pending |
| PSRAM | 16 MiB |
| SRAM | 640 KiB total configured region across AP/CP/shared areas |

The local `bk_avdk_smp_v3.1.1.8_20260605` delivery is modified reference
material and does not define this identity. The TiRTC package is vendored under
`third_party/tirtc`. Its private media security implementation remains inside
that package. A build with another platform SDK, compiler ABI, TiRTC archive,
or memory map is a different artifact and must be validated again.

The active archive is the package built for the official SDK Mbed TLS 3.5.2
ABI. Its TiRTC SSL and DTLS features are disabled; the platform provider is
still required for the archive's AES, DES, SHA and MD5 references. Older
archives are not valid A/B candidates because their feature and crypto ABI
contracts differ even when the final link succeeds. The one-object SDP
compatibility experiment has been retired. Runtime video behavior must be
revalidated with this complete replacement archive before further bisection.
The SDK delta is reproducible: `tools/build.sh` applies
`tools/patches/bk-avdk-lwip-tcp-pcb-12.patch`. This changes
`MAX_SOCKETS_TCP` from 8 to 12, which also changes `MEMP_NUM_TCP_PCB` from 8
to 12 and `MEMP_NUM_NETCONN` from 20 to 24. It does not increase the configured
80 KiB lwIP heap or the per-connection TCP windows.

`tools/patches/bk-avdk-gc0308-20fps.patch` changes the official GC0308 profile
from 24 MHz/FPS25 to 20 MHz/FPS20. The product requests FPS20 from DVP and the
H.264 timing block, sends every encoded frame, and reports measured fps/kbps at
runtime. This clock-derived frame rate remains subject to board measurement.

`tools/patches/bk-avdk-touch-read-failure-release.patch` changes the official
FT6336 polling path so an actual controller read failure emits at most one
synthetic release for an active contact. This prevents a lost physical release
from leaving push-to-talk or navigation pressed while avoiding repeated release
events when the application queue is merely empty.

`tools/patches/bk-avdk-wifi-skb-dequeue-guard.patch` hardens the official CP
Wi-Fi temporary transmit queue against a broken `next`/`prev` chain observed
during media disconnect. It detaches the current SKB for the original caller's
send/free path and resets the damaged batch head instead of allowing
`__skb_unlink` to dereference address `0x4` and reboot the device.
