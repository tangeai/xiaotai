#!/usr/bin/env python3
"""Reject a linked monitor app or a missing XiaoTai/P4 media implementation."""
import os
from pathlib import Path
import subprocess
import sys

elf = Path(sys.argv[1])
output = subprocess.check_output([os.environ["CROSS_NM"], "--defined-only", str(elf)], text=True)
names = [line.split()[-1] for line in output.splitlines() if line.split()]
required = {
    "app_main", "starter_runtime_start", "starter_product_start", "starter_media_init",
    "starter_voice_start", "starter_voice_feed_pcm16k", "wifi_manager_start",
    "platform_client_start", "p4_video_submit", "camera_pipeline_set_rtc_video_enabled",
    "call_video_renderer_submit_h264", "call_video_renderer_submit_mjpeg",
    "starter_tirtc_send_h264", "starter_runtime_call_contact_video",
}
for name in required:
    if name not in names:
        raise SystemExit(f"FAIL: missing product implementation {name}")
if names.count("app_main") != 1:
    raise SystemExit("FAIL: entry point is not unique")
for forbidden in ("app_init", "app_run", "device_call_init", "rtc_transport_init",
                  "wechat_voip_service_init", "esp_camera_init"):
    if forbidden in names:
        raise SystemExit(f"FAIL: foreign application or S3 camera owner {forbidden}")
print("PASS: XiaoTai entry/runtime/UI/wake and P4 media linked; monitor business excluded")
