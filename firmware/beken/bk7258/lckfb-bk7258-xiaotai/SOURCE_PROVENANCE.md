# Source provenance

## Platform baseline

- Source: <https://gitee.com/bekencorp/bk_avdk_smp.git>
- Tag: `release/v3.1.1.8`
- Commit: `1cfd56af09a3cb6470f35f1e0c604035ed1b6ee7`
- Target: default LCKFB BK7258 development board configuration
- Reused facts: AP/CP build layout, configuration baseline, Flash partitions,
  16 MiB PSRAM regions, DVP/audio component selection, and minimal CP entry.

The local folder
`.references/BK7258/bk_avdk_smp_v3.1.1.8_20260605` contains a
private Tange component and changes to audio, DVP, H.264, lwIP, build options and
project configuration. It is retained only as implementation and problem-history
reference. It is not the SDK baseline and must not be described as byte-identical
to the official Gitee tag.

The older `bk_avdk` checkout and `libtgCloud-kcp_bk7258.a` example are evidence
for BK7258 media and platform behavior only. They are not linked into this
product and do not define the TiRTC business API.

## TiRTC

- Source package:
  `tirtc__beken-bk7258__gcc-arm-none-eabi-10.3-2021.10__v2.5.0__mini`
- Vendored destination: `third_party/tirtc`
- Archive SHA-256:
  `47e26827ca2419084163e3656dca4d6e508e433381784e9c03f232d3dbaa4171`
- Package target: `beken-bk7258`
- Package build baseline: `BK-AVDK 3.1.1.8-official-1cfd56af`, matching the
  product's official public SDK baseline

## Product behavior

The ESP32 XiaoTai implementations are behavioral references for provisioning,
verification-code binding, TiRTC lifecycle, stream/call/voip capability
reporting, session arbitration, media semantics, logging, and acceptance tests.
No ESP-IDF driver, task, allocator, DMA, display, camera, audio, or memory-layout
implementation is copied into this BK7258 project.

## TiRTC crypto boundary

The package disables both SSL and DTLS and declares the official BK SDK
`psa_mbedtls` component (Mbed TLS 3.5.2) as its platform crypto provider for
AES, DES, SHA and MD5 references. It does not bundle Mbed TLS. Application
WebClient TLS, when enabled, is an independent platform facility. The
application does not include Mbed TLS headers or link a project-vendored
archive. The current open deployment is discovered through
`http://ep-open.tangeopen.com/services` and returns `http://` REST/TiRTC plus
`mqtt://` MQTT endpoints. Device-request authentication uses the BK SDK
`hmac_sha_256` component.

The build gate requires the final AP ELF to resolve `mbedtls_md5` from
`libpsa_mbedtls.a` and rejects any linked `mbedtls_x509write_crt_init` symbol.
The AP+CP final package passes this gate. Runtime board validation remains a
separate release gate.
