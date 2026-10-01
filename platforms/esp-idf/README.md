# ESP-IDF platform project

ESP32-S3 and ESP32-P4 share a platform family but use target-specific TiRTC
archives and toolchains. During migration, board manifests dispatch to the
existing ESP-IDF projects. Board drivers will move behind the stable media and
control interfaces before complete projects are consolidated.

The platform adapter owns FreeRTOS, ESP-IDF networking/storage, allocation
capabilities, and target startup. A board adapter owns GPIO, buses, codecs,
display, touch, camera, DMA, clocks, and power sequencing. Product protocol and
session state remain outside both.

The two Waveshare ESP32-P4 projects now compile one `waveshare_p4/hardware_board.c`
and use its shared `hardware_board.h` interface.
They also compile one family-owned `waveshare_p4/call_video_renderer.c`; each
target retains only `call_video_renderer_config.h`, so viewport, reservoir and
scheduling choices stay explicit without duplicating the renderer state machine.
Their per-board BSP, display and audio facts live under `boards/waveshare/`;
shared family wiring and camera defaults live in `waveshare_p4/board_defaults.h`.
The original project-local config headers remain compatibility includes.
The ATK ESP32-S3 media component compiles its implementation from
`boards/alientek/atk-dnesp32s3/`, with audio/video contracts reading that
source and its wiring config. The LCKFB SZPI ESP32-S3 media and product
components compile I2C/PCA9557, camera, I2S/codec, display and touch adapters
from `boards/lckfb/esp32s3/`; AEC, codec transport, session and LVGL page
policy remain in the existing components. ATK, SZPI, and both Waveshare P4
projects use the SDK-neutral `../common/include/xiaotai_board_audio.h`
interface for lifecycle, PCM operations, and semantic capture layout. ATK and
SZPI additionally use `xiaotai_board_camera.h`, which keeps vendor frame types
inside board implementations while making borrowed-frame ownership explicit.
All four projects compile the single NVS credential adapter in
`components/runtime_config`; project-local component directories are only thin
ESP-IDF registration entries and do not own another copy of the implementation.
SZPI and both P4 projects likewise compile the full discovery/HTTP/MQTT client
from `components/platform_client`, instead of treating the SZPI project as a
source library. ATK intentionally retains its smaller client variant until its
product lifecycle is migrated to the full service contract.
The shared `starter_button` component owns debounce and the AI-toggle intent,
while `xiaotai_board_button.h` and each selected board adapter own GPIO setup
and active-level normalization. No shared button policy contains board GPIOs.
SZPI and both P4 products also compile the full TiRTC SDK boundary from
`components/starter_tirtc`; their project-local components are registration
shims only. ATK keeps its intentionally smaller TiRTC lifecycle variant until
that product adopts the same session contract.
Their shared session arbitration and diagnostic console likewise live in
`components/starter_runtime` and `components/starter_console`. This keeps the
product state machine independent of whichever board project was ported first.
The common SoftAP/STA and captive-DNS implementation is in
`components/wifi_manager`; ATK retains its separate provisioning variant.
The shared XiaoTai startup composition root and its product Kconfig are under
`main`; target projects only register it and declare genuinely target-specific
component dependencies such as `p4_hardware`.
The shared rich-product media contract, AEC boundary, anti-alias resampler and
AI pre-roll queue live in `components/starter_media_common`. Each board keeps
its own `starter_media.c` for codec, camera, DMA and power sequencing and
compiles those common algorithms into that board-owned component.
The Voicute wake engine is canonical in `components/starter_voice`; each target
wrapper supplies its own model bundle (`head.h` plus the embedded TFLite file),
so engine policy is shared without preventing board/product model variants.
Product UI, session presentation and generated fonts are canonical in
`components/starter_product`. Target wrappers inject board display dependencies
and the selected prompt/ring asset bundle; display buses and power sequencing
remain in board adapters.
All four ESP-IDF target projects pass their full builds; this is not
hardware-media verification. BK audio also consumes the common lifecycle and
PCM contract; its other peripherals remain behind Beken-specific board adapters.
