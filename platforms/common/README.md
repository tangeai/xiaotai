# Common board contracts

This directory contains C contracts that do not depend on ESP-IDF, Beken SDK,
FreeRTOS, or a particular device SDK. Board adapters implement them; product
media code consumes them.

- `xiaotai_board_audio.h` defines audio lifecycle, PCM frame operations, and
  the semantic capture layout (mono, duplicated mic, or mic/reference).
- `xiaotai_board_camera.h` defines camera lifecycle and borrowed-frame
  ownership without exposing `camera_fb_t` or another vendor frame type.

The contracts deliberately stop at hardware ownership. AEC, G.711/JPEG
conversion, TiRTC pacing, backpressure, and session policy remain in platform
or product modules.
