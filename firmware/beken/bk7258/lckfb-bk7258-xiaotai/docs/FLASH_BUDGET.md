# BK7258 Flash budget

## Current layout

The current partition table declares an 8 MiB Flash address space. Runtime
JEDEC identification is still pending, so this is build configuration evidence,
not a measured component claim. The product image is constrained by the AP code
partition rather than by the configured device capacity.

| Partition | Size | Purpose |
| --- | ---: | --- |
| `primary_bootloader` | 68 KiB | bootloader |
| `primary_cp_app` | 1360 KiB | CP firmware |
| `primary_ap_app` | 2720 KiB | XiaoTai application |
| `resources` | 1536 KiB | fonts, expressions and prompt audio |
| `usr_config` | 60 KiB | product configuration |
| fixed EasyFlash/RF/network areas | 24 KiB | persistent SDK data |

Another 2424 KiB remains unallocated before the fixed EasyFlash area. It can
be assigned later without moving the bootloader, CP or AP start addresses.

OTA is excluded from this product build.  The former OTA window is split into
additional AP code space and the resource partition.

## Verified build

- AP linked flash: 1,926,900 bytes with the complete basic-CJK UI font and the
  embedded two-step provisioning portal. The
  2720 KiB encoded partition provides a 2560 KiB effective code region, so the
  current release image occupies 73.51%.
- CP linked flash: 990,868 bytes, 75.60%.
- AP RAM: 164,516 bytes, 47.82%.
- CP RAM: 127,292 bytes, 52.29%.
- Release `all-app.bin` SHA-256:
  `152320da9cd631568dbe1ce80b554271995f20947c5da991f462dbe9234fbb03`.
- Release `app_pack.rbl` SHA-256:
  `9967da5d52e13303ef5c544ae0e2f8aaf85625f3886313e8796cce05efa5df5c`.

The 1536 KiB resource partition is reserved for replaceable expression packs,
prompt audio, and a future independently updatable font. The first release
links its one-bit basic CJK font into AP so glyph lookup is memory-mapped and
requires no internal SRAM allocation.

The previous AP image was 1,004,224 bytes in a 1156 KiB code partition.  The
release configuration removes the AP BLE host, HTTP OTA, command line tests,
iperf, FATFS/SD-card support and unused SDK demos. Device signing, TiRTC KCP
media, the BK platform crypto provider, Wi-Fi provisioning, audio/video, logs
and crash backtraces remain enabled. The active open service discovery and
product endpoints use HTTP/MQTT; TiRTC package SSL and DTLS are disabled.

## Large retained components

- TiRTC library: about 304 KiB.
- BK PSA Mbed TLS 3.5.2: supplies the TiRTC package's remaining AES, DES, SHA
  and MD5 references and the independently configured BK WebClient TLS path.
  Device request signing separately uses the BK HMAC component.
- Basic-CJK glyph data is the largest product-owned asset. Binding-code audio
  is fetched from the authenticated server as 8 kHz mono PCM and is never
  stored as local digit prompts.

AVDK's dynamic lwIP allocator directly references memory-statistics fields, so
`CONFIG_LWIP_MEM_STATS` and `CONFIG_LWIP_MEMP_STATS` must stay enabled.
