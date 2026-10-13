#!/usr/bin/env python3
"""Exercise the P4 MJPEG converter's session fit and rotated crop geometry."""

from pathlib import Path
import re
import subprocess
import tempfile

repo = Path(__file__).resolve().parents[2]
converter = (repo / "platforms/esp-idf/waveshare_p4/video_frame_converter.c").read_text()
renderer = (repo / "platforms/esp-idf/waveshare_p4/call_video_renderer.c").read_text()
video = (repo / "firmware/esp-idf/esp32p4/waveshare-touch-lcd-43c/components/p4_hardware/p4_video.c").read_text()


def function(source, signature):
    start = source.index(signature)
    return source[start:source.index("\n}", start) + 2]


source = r'''
#include <assert.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#define ESP_OK 0
#define ESP_ERR_INVALID_ARG -1
#define ESP_ERR_INVALID_SIZE -2
#define TAG "fit-test"
#define ESP_RETURN_ON_FALSE(condition, error, tag, message) \
    do { (void)(tag); (void)(message); if (!(condition)) return (error); } while (0)
#define taskENTER_CRITICAL(lock) ((void)(lock))
#define taskEXIT_CRITICAL(lock) ((void)(lock))
typedef int esp_err_t;
typedef enum { VIDEO_FRAME_FIT_CONTAIN, VIDEO_FRAME_FIT_COVER } video_frame_fit_mode_t;
typedef enum { VIDEO_FRAME_ROTATION_CLOCKWISE_0=0,
               VIDEO_FRAME_ROTATION_CLOCKWISE_90=90,
               VIDEO_FRAME_ROTATION_CLOCKWISE_180=180,
               VIDEO_FRAME_ROTATION_CLOCKWISE_270=270 } video_frame_rotation_t;
typedef struct video_frame_converter {
    struct { uint16_t output_width, output_height; video_frame_fit_mode_t fit_mode; } config;
    int stats_lock;
    struct { uint16_t last_crop_x,last_crop_y,last_crop_width,last_crop_height; } stats;
} *video_frame_converter_handle_t;
static bool video_frame_rotation_swaps_axes(video_frame_rotation_t rotation) {
    return rotation==90 || rotation==270;
}
static bool video_frame_fit_mode_is_valid(video_frame_fit_mode_t mode) {
    return mode==VIDEO_FRAME_FIT_CONTAIN || mode==VIDEO_FRAME_FIT_COVER;
}
'''
source += function(converter, "esp_err_t video_frame_converter_set_fit_mode(")
source += function(converter, "static void video_frame_fit_inside(")
source += function(converter, "static esp_err_t video_frame_apply_fit_crop(")
source += r'''
static void check(video_frame_converter_handle_t handle, int rotation,
                  int expected_crop_x, int expected_crop_width,
                  int expected_render_width, int expected_offset_x) {
    uint16_t x=0,y=0,w=640,h=480,rw,rh,ox,oy;
    assert(video_frame_apply_fit_crop(handle, rotation, false, 640,480,
                                      &x,&y,&w,&h)==ESP_OK);
    assert(x==expected_crop_x && y==0 && w==expected_crop_width && h==480);
    assert(handle->stats.last_crop_x==x && handle->stats.last_crop_width==w);
    video_frame_fit_inside(rotation==90 || rotation==270 ? h:w,
                           rotation==90 || rotation==270 ? w:h,
                           640,480,false,&rw,&rh,&ox,&oy);
    assert(rw==expected_render_width && ox==expected_offset_x && rh==480 && oy==0);
}
int main(void) {
    struct video_frame_converter converter={.config={.output_width=640,.output_height=480}};
    assert(video_frame_converter_set_fit_mode(&converter,VIDEO_FRAME_FIT_CONTAIN)==ESP_OK);
    check(&converter,0,0,640,640,0);
    check(&converter,90,0,640,360,140);
    check(&converter,180,0,640,640,0);
    check(&converter,270,0,640,360,140);
    assert(video_frame_converter_set_fit_mode(&converter,VIDEO_FRAME_FIT_COVER)==ESP_OK);
    check(&converter,90,140,360,640,0);
    assert(video_frame_converter_set_fit_mode(&converter,VIDEO_FRAME_FIT_CONTAIN)==ESP_OK);
    check(&converter,270,0,640,360,140);
    assert(video_frame_converter_set_fit_mode(&converter,99)==ESP_ERR_INVALID_ARG);
    assert(converter.config.fit_mode==VIDEO_FRAME_FIT_CONTAIN);
    assert(video_frame_converter_set_fit_mode(0,VIDEO_FRAME_FIT_COVER)==ESP_ERR_INVALID_ARG);
    return 0;
}
'''

with tempfile.TemporaryDirectory(prefix="p4-mjpeg-fit-") as directory:
    path = Path(directory)
    (path / "test.c").write_text(source)
    subprocess.run(["cc", "-std=c11", "-Wall", "-Wextra", "-Werror",
                    str(path / "test.c"), "-o", str(path / "test")], check=True)
    subprocess.run([str(path / "test")], check=True)

assert "presentation.fit_cover = mode == STARTER_TIRTC_VOIP;" in video
assert re.search(r"video_frame_converter_set_fit_mode\(\s*s_renderer\.mjpeg_converter,", renderer)
assert "CALL_VIDEO_MJPEG_FIT_MODE" not in renderer
print("PASS: CALL MJPEG contains four rotations; VoIP covers; converter resets between sessions")
