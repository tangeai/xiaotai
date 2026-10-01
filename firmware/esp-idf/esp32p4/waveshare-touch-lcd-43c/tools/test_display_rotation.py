#!/usr/bin/env python3
from pathlib import Path

root = Path(__file__).resolve().parents[1]
driver = (root / "main/drivers/display/display_driver.c").read_text()
defaults = (root / "sdkconfig.defaults").read_text()
renderer = (Path(__file__).resolve().parents[5] / "platforms/esp-idf/waveshare_p4/call_video_renderer.c").read_text()
renderer_config = (root / "main/services/call_video_renderer_config.h").read_text()

assert "#define DISPLAY_DRIVER_LANDSCAPE_ROTATION LV_DISP_ROT_270" in driver
assert "#define DISPLAY_DRIVER_DRAW_LINES 96" in driver
assert "pair & 0x00ff00ffU" in driver
assert "lv_disp_set_rotation(s_display, DISPLAY_DRIVER_LANDSCAPE_ROTATION);" in driver
assert "LVGL rotates rendering and input with DISPLAY_DRIVER_LANDSCAPE_ROTATION" in driver
assert "CONFIG_APP_MEDIA_COMPACT_HEALTH_LOG=y" in defaults
assert "call_video_note_received(trace_received_at_us, pts, true);" in renderer
assert "CALL_VIDEO_MJPEG_ADAPTIVE_PLAYOUT != 0U" in renderer
assert "CALL_VIDEO_MJPEG_ADAPTIVE_PLAYOUT    0U" in renderer_config
assert "CALL_VIDEO_MJPEG_TASK_PRIORITY       6U" in renderer_config

print("PASS: 4.3C display and touch use the 180-degree-reversed landscape orientation; compact media FPS telemetry enabled")
