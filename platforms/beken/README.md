# Beken platform project

BK7258/BK7259 remain an independent SDK project because AP/CP images, toolchain,
partitions, RTOS, media drivers, TLS ABI, and TiRTC archive differ from ESP-IDF.
The shared contract is a small C interface plus board manifests and product
tests, not shared SDK source.

During migration, Beken manifests dispatch to their existing project. Board
adapters own peripherals and resource placement; the stable product modules own
provisioning, HTTP/MQTT, the single TiRTC lifecycle, and session arbitration.

The BK7258 expansion-board pins and display/touch dimensions now live in
`boards/lckfb/bk7258/board_config.h`; the former
`ap/include/xiaotai_board.h` compatibility include has been removed. This is a
configuration seam only. AP/CP still use the existing project, and a passing
host contract does not establish a final firmware link or on-device
TiRTC/TLS compatibility.
