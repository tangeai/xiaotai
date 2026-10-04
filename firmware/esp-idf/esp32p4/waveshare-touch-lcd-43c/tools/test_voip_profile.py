#!/usr/bin/env python3
"""Verify the unified device capability snapshot submitted after MQTT online."""
import ast
import json
import re
from pathlib import Path

root = Path(__file__).resolve().parents[1]
repo_root = Path(__file__).resolve().parents[5]
source = (repo_root / "platforms/esp-idf/components/starter_runtime/src/starter_runtime.c").read_text()
start = source.index("static void request_device_profile(void)")
profiles = re.findall(r"static const char profile\[\] =\s*(.*?);", source[start:], re.S)
def decode(text):
    return json.loads("".join(ast.literal_eval(s) for s in re.findall(r'"(?:\\.|[^"\\])*"', text)))
p4, s3 = map(decode, profiles[:2])
p4_manifest = json.loads((repo_root / "boards/waveshare/esp32p4-touch-lcd-43c/board.json").read_text())
s3_manifest = json.loads((repo_root / "boards/lckfb/esp32s3/board.json").read_text())
def project_version(project_dir):
    match = re.search(r'set\(PROJECT_VER\s+"([^"]+)"\)',
                      (project_dir / "CMakeLists.txt").read_text())
    assert match is not None
    assert re.fullmatch(r"[0-9]+\.[0-9]+\.[0-9]+\+build\.[1-9][0-9]*",
                        match.group(1))
    return match.group(1)
assert p4["hardware"] == {
    "chip_model": "ESP32-P4",
    "board_model": p4_manifest["id"],
}
assert p4["firmware_version"] == project_version(repo_root / p4_manifest["project_dir"])
assert set(p4["profiles"]) == {"stream", "call", "voip"}
stream, call, voip = (p4["profiles"][name] for name in ("stream", "call", "voip"))
assert (stream["up_audio_streamid"], stream["up_video_streamid"]) == (10, 11)
assert (stream["down_audio_streamid"], stream["down_video_streamid"]) == (10, 11)
assert (stream["up_audio_mt"], stream["up_video_mt"]) == (["alaw"], ["h264"])
assert (stream["down_audio_mt"], stream["down_video_mt"]) == (["alaw"], ["h264"])
assert stream["camera_rotation"] == 270
assert stream["aspect_ratio"] == 720 / 960
assert (call["up_audio_mt"], call["up_video_mt"]) == (["alaw"], ["h264"])
assert (call["down_audio_mt"], call["down_video_mt"]) == (["alaw"], ["h264"])
assert call["camera_rotation"] == 270
assert voip["no_video"] is False
assert (voip["up_video_mt"], voip["down_video_mt"]) == ("h264", "mjpeg")
assert (voip["screen_width"], voip["screen_height"]) == (640, 480)
assert voip["camera_rotation"] == 0
assert voip["down_video_rotation"] == 1
assert voip["aspect_ratio"] == 960 / 1280
assert voip["hor_mirror"] is False and voip["vert_mirror"] is False
assert voip["object_fit"] == "contain" and voip["video_res_mode"] == "fit_screen"
assert len(json.dumps(p4, separators=(",", ":")).encode()) <= 16 * 1024
assert s3["hardware"] == {
    "chip_model": "ESP32-S3",
    "board_model": s3_manifest["id"],
}
assert s3["firmware_version"] == project_version(repo_root / s3_manifest["project_dir"])
assert set(s3["profiles"]) == {"stream", "call", "voip"}
assert s3["profiles"]["voip"]["no_video"] is True
assert s3["profiles"]["voip"]["down_video_mt"] == "none"
profile_function = source[start:source.index("static void handle_voip_profile", start)]
assert 'PLATFORM_SERVICE_DEVICE, "/v1/device/profile"' in profile_function
assert "/v1/voip/device/profile" not in profile_function
assert "stream_rotation=270" in profile_function
renderer = (Path(__file__).resolve().parents[5] / "platforms/esp-idf/waveshare_p4/call_video_renderer.c").read_text()
renderer_config = (root / "main/services/call_video_renderer_config.h").read_text()
wechat_config = (root / "main/services/wechat_voip/wechat_voip_config.h").read_text()
assert "call_video_renderer_set_presentation" in renderer
assert "s_renderer.presentation.rotation" in renderer
assert not re.search(r"video_frame_rotation_t display_rotation\s*=\s*VIDEO_FRAME_ROTATION_CLOCKWISE_90;", renderer)
assert "starter_media_set_remote_video_presentation" in source
presentation_start = source.index("static void configure_remote_video_presentation")
presentation = source[presentation_start:
                      source.index("static void request_ai_token_response",
                                   presentation_start)]
assert "uint16_t rotation = 90U;" in presentation
assert "call_read_rotation" not in presentation
assert 'wechat ? "wechat-contract" : "local-default"' in presentation
assert "#define APP_CONFIG_WECHAT_VOIP_CAMERA_ROTATION 0" in wechat_config
assert '"rotation=%s source_rotation=%s "' in renderer
assert "CALL_VIDEO_RENDER_WIDTH             640U" in renderer_config
assert "CALL_VIDEO_RENDER_HEIGHT            384U" in renderer_config
assert "CALL_VIDEO_MJPEG_MAX_PIXELS        (640U * 480U)" in renderer
print("PASS: scene-specific uplink/downlink rotation and centered 640x384 presentation contract")
