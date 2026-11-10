# Glossary

[中文](glossary.md) · [English](glossary.en.md)

Documents explain terms on first use. This page collects common terms for reference.

| Term | Meaning |
| --- | --- |
| A-law | An audio coding format used by this project; sample rate and packet size vary by feature |
| AEC | Acoustic echo cancellation; reduces speaker sound picked up by the microphone |
| AI | Artificial intelligence; this project includes voice conversations and audio recognition, depending on board firmware |
| AGC | Automatic gain control; adjusts audio signal amplitude |
| AP / CP | Application core / communication core in BK7258 documentation |
| API | Application programming interface; defines requests and responses between software components |
| Board ID | A board's unique repository identifier, used for build selection, packages, and firmware matching |
| BSP | Board support package; basic drivers and configuration for specific hardware |
| DMA | Direct memory access; transfers peripheral data without the processor copying every byte |
| ESP-IDF | Espressif IoT Development Framework for ESP32 firmware |
| Flash | Nonvolatile storage for firmware and data |
| GPIO | General-purpose input/output pins for buttons, power control, and peripherals |
| H5 | The browser interface; here, the device live-view and two-way audio page |
| Hardware IR | Hardware information record containing devices, pins, evidence sources, and confidence grades |
| HIL | Hardware-in-the-loop testing, using a physical board in automated or manual validation |
| H.264 | A video compression format using keyframes and reference frames to reduce data volume |
| HTTP | Hypertext Transfer Protocol, commonly used for device/server requests |
| I²C | A serial bus commonly used for touch, audio, and sensor components |
| I²S | A digital audio bus for microphone and speaker data |
| MQTT | A lightweight messaging protocol for device commands, notifications, and status reports |
| MJPEG | A sequence of JPEG video frames, each independently decodable |
| manifest | Structured metadata describing boards, dependencies, or release packages |
| NS | Noise suppression; reduces background noise in audio |
| NVS | Nonvolatile key-value storage for Wi-Fi, binding, and other settings |
| EP | This project's platform service-discovery endpoint, used to retrieve service addresses |
| OTA | Over-the-air firmware updates through the network |
| PCB | Printed circuit board; revisions may change pins or components |
| PSRAM | Pseudo-static RAM, commonly used for media buffers and large runtime allocations |
| PCM | Pulse-code modulation; generally uncompressed digital audio in this project |
| Opus | A variable-length audio codec suitable for real-time speech |
| SDK | Software development kit, including libraries, headers, tools, and examples |
| TiRTC | The real-time audio/video SDK used by XiaoTai |
| VoIP | Voice over Internet Protocol; primarily WeChat calling integration here |
| WHIP | A protocol for establishing real-time media connections over HTTP; used here to create TiRTC sessions |
| WSL2 | Windows Subsystem for Linux, providing a Linux build environment on Windows |

Codec names, component models, and protocol fields usually remain untranslated. Guides explain their role in each feature rather than expecting users to infer it from abbreviations.
