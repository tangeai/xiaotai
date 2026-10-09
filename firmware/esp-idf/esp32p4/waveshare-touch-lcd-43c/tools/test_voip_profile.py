#!/usr/bin/env python3
"""Verify the unified device capability snapshot submitted after MQTT online."""
import ast
import json
import re
import subprocess
import tempfile
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
assert (stream["down_audio_streamid"], stream["down_video_streamid"]) == (14, 15)
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
assert voip["camera_rotation"] == 180
assert voip["down_video_rotation"] == 0
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
s3_stream = s3["profiles"]["stream"]
assert s3_stream["no_video"] is False, "S3 H5 still produces camera MJPEG"
assert s3_stream["up_video_mt"] == ["mjpeg"]
assert (s3_stream["up_audio_streamid"], s3_stream["up_video_streamid"]) == (10, 11)
assert (s3_stream["down_audio_streamid"], s3_stream["down_video_streamid"]) == (14, 15)
bk_source = (repo_root / "firmware/beken/bk7258/lckfb-bk7258-xiaotai/ap/src/xiaotai_platform_client.c").read_text()
bk_profile = decode(re.search(r"static const char s_device_profile\[\] =\s*(.*?);", bk_source, re.S).group(1))
bk_stream = bk_profile["profiles"]["stream"]
assert (bk_stream["up_audio_streamid"], bk_stream["up_video_streamid"]) == (10, 11)
assert (bk_stream["down_audio_streamid"], bk_stream["down_video_streamid"]) == (14, 15)
assert s3_stream["audio_rate"] == 8000
assert s3["profiles"]["call"]["no_video"] is True
assert s3["profiles"]["voip"]["no_video"] is True
assert s3["profiles"]["voip"]["down_video_mt"] == "none"
profile_function = source[start:source.index("static void handle_voip_profile", start)]
assert 'PLATFORM_SERVICE_DEVICE, "/v1/device/profile"' in profile_function
assert "/v1/voip/device/profile" not in profile_function
assert "stream_rotation=270" in profile_function
assert "voip_down_rotation=90 down_rotation_mode=0" in profile_function
legacy_api_source = (root / "main/services/wechat_voip/wechat_voip_api.c").read_text()
legacy_api_header = (root / "main/services/wechat_voip/wechat_voip_api.h").read_text()
legacy_thing_source = (root / "main/services/wechat_voip/wechat_voip_thing.c").read_text()
runtime_contract = (root / "tirtc-runtime-contract.json").read_text()
for legacy_source in (
    legacy_api_source,
    legacy_api_header,
    legacy_thing_source,
    runtime_contract,
):
    assert "/v1/voip/device/profile" not in legacy_source
assert "wechat_voip_api_report_profile" not in legacy_api_source
assert "wechat_voip_api_report_profile" not in legacy_api_header
assert "report_profile()" not in legacy_thing_source
assert "profile_ready" not in legacy_thing_source
renderer = (Path(__file__).resolve().parents[5] / "platforms/esp-idf/waveshare_p4/call_video_renderer.c").read_text()
renderer_config = (root / "main/services/call_video_renderer_config.h").read_text()
wechat_config = (root / "main/services/wechat_voip/wechat_voip_config.h").read_text()
assert "call_video_renderer_set_presentation" in renderer
assert "s_renderer.presentation.rotation" in renderer
assert not re.search(r"video_frame_rotation_t display_rotation\s*=\s*VIDEO_FRAME_ROTATION_CLOCKWISE_90;", renderer)
assert "starter_media_set_remote_video_presentation" in source
rotation_start = source.index("static uint16_t remote_video_initial_rotation(")
rotation_function = source[rotation_start:
                           source.index("\n}", rotation_start) + 2]
rotation_test = f'''\
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
{rotation_function}
int main(void) {{
    assert(remote_video_initial_rotation(false) == 0U);
    assert(remote_video_initial_rotation(true) == 90U);
    return 0;
}}
'''
with tempfile.TemporaryDirectory(prefix="remote-video-rotation-") as directory:
    path = Path(directory)
    (path / "test.c").write_text(rotation_test)
    subprocess.run(["cc", "-std=c11", "-Wall", "-Wextra", "-Werror",
                    str(path / "test.c"), "-o", str(path / "test")], check=True)
    subprocess.run([str(path / "test")], check=True)
presentation_start = source.index("static void configure_remote_video_presentation")
presentation = source[presentation_start:
                      source.index("static void request_ai_token_response",
                                   presentation_start)]
assert "remote_video_initial_rotation(wechat)" in presentation
assert "call_read_rotation" not in presentation
assert 'wechat ? "wechat-contract" : "device-call-default"' in presentation
assert "#define APP_CONFIG_WECHAT_VOIP_CAMERA_ROTATION 180" in wechat_config
assert '"rotation=%s source_rotation=%s "' in renderer
assert "CALL_VIDEO_RENDER_WIDTH             640U" in renderer_config
assert "CALL_VIDEO_RENDER_HEIGHT            480U" in renderer_config
assert "CALL_VIDEO_MJPEG_MAX_PIXELS        (640U * 480U)" in renderer
print("PASS: scene-specific rotation and full-height 640x480 presentation contract")
