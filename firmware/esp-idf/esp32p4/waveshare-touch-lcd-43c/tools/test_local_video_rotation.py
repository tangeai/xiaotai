#!/usr/bin/env python3
"""Device-call video exposes a session-local clockwise rotation control."""

from pathlib import Path


REPO = Path(__file__).resolve().parents[5]
PROJECT = Path(__file__).resolve().parents[1]
PROJECT_CMAKE = PROJECT / "CMakeLists.txt"
RENDERER_H = REPO / "platforms/esp-idf/waveshare_p4/call_video_renderer.h"
RENDERER_C = REPO / "platforms/esp-idf/waveshare_p4/call_video_renderer.c"
P4_VIDEO_H = REPO / "platforms/esp-idf/waveshare_p4/p4_video.h"
P4_VIDEO_C = PROJECT / "components/p4_hardware/p4_video.c"
PRODUCT = REPO / "platforms/esp-idf/components/starter_product/src/starter_product.c"
RUNTIME = REPO / "platforms/esp-idf/components/starter_runtime/src/starter_runtime.c"


def section(text: str, start: str, end: str) -> str:
    begin = text.index(start)
    return text[begin:text.index(end, begin)]


renderer_h = RENDERER_H.read_text()
renderer_c = RENDERER_C.read_text()
p4_video_h = P4_VIDEO_H.read_text()
p4_video_c = P4_VIDEO_C.read_text()
product = PRODUCT.read_text()
product_runtime = RUNTIME.read_text()
project_cmake = PROJECT_CMAKE.read_text()

assert "call_video_renderer_rotate_clockwise(uint16_t *rotation)" in renderer_h
assert "call_read_rotation" not in product_runtime[
    product_runtime.index("static void configure_remote_video_presentation"):
    product_runtime.index("static void request_ai_token_response")
]
rotate = section(
    renderer_c,
    "esp_err_t call_video_renderer_rotate_clockwise(",
    "esp_err_t call_video_renderer_start_for_codec(",
)
assert "s_renderer.running" in rotate
assert "% 360U" in rotate
assert "remote_profile = false" in rotate
assert "*rotation = next_rotation" in rotate

assert "p4_video_rotate_remote_clockwise(uint16_t *rotation)" in p4_video_h
assert "call_video_renderer_rotate_clockwise(rotation)" in p4_video_c

assert "#define PRODUCT_CALL_ROTATE_SIZE_PX 48" in product
assert "ACTION_CALL_ROTATE" in product
assert "LV_SYMBOL_REFRESH" in product
assert "lv_obj_set_style_border_color(s_call_rotate_button" in product
assert 'lv_color_hex(0x72DEF8)' in product
assert "p4_video_rotate_remote_clockwise(&rotation)" in product
assert '"已旋转 %u°"' in product

action = section(product, "static void on_action(", "static lv_obj_t *make_button(")
assert "!product.call_wechat" in action

controls = section(product, "static void refresh_call_controls(",
                   "static void render_call_result(")
assert "!product->call_wechat" in controls
assert "set_object_visible(s_call_rotate_button" in controls

assert "s_call_rotation_hint_hide_ms = 0" in product
assert "VIDEO_FRAME_FIT_CONTAIN" in renderer_c
assert 'set(PROJECT_VER "1.0.0+build.17")' in project_cmake

print("PASS: device-call video rotation is local, clockwise, and session-scoped")
