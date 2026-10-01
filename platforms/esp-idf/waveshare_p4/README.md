# Waveshare ESP32-P4 family adapter

This directory owns the hardware implementation shared by the supported
Waveshare ESP32-P4 display boards. Per-board pin, codec, and panel choices stay
in each board's `hardware_board_config.h`; product session and UI policy stay
in the project components.

- `hardware_board.*` owns shared buses, power rails, and board capabilities.
- `camera_driver.*` owns the MIPI CSI/V4L2 device and borrowed USERPTR frame
  lifetime. A frame remains owned by the caller until `camera_driver_release`.
- `app_memory_policy.*` and `media_dma_reserve.*` own P4 internal/DMA/PSRAM
  allocation policy and the early DMA escrow.
- `video_frame_converter.*` and `video_yuv420_scaler.*` own the shared P4 pixel
  conversion and scaling primitives; session-specific encoder policy remains
  in each project's camera pipeline.
- `p4_video_capture.*` is an opt-in diagnostic at the encoded-video boundary.
  It is not part of camera ownership or normal media policy.

The two P4 projects intentionally retain separate `p4_video.c` files because
their display presentation and H5/VoIP media policies differ. Those orchestration
differences must not be hidden behind a shallow hardware adapter. Their common
component source set and public `p4_video.h` contract are declared once in this
directory.
