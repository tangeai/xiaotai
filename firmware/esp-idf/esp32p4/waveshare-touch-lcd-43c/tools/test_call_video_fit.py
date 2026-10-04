#!/usr/bin/env python3
"""Device-call downlink preserves the decoded frame and contains it centrally."""
from pathlib import Path
import subprocess
import tempfile

repo = Path(__file__).resolve().parents[5]
project = Path(__file__).resolve().parents[1]
renderer = (repo / "platforms/esp-idf/waveshare_p4/call_video_renderer.c").read_text()
converter = (repo / "platforms/esp-idf/waveshare_p4/video_frame_converter.c").read_text()
config = (project / "main/services/call_video_renderer_config.h").read_text()


def function(text, signature):
    start = text.index(signature)
    return text[start:text.index("\n}", start) + 2]


code = r'''
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#define ESP_OK 0
#define ESP_ERR_INVALID_ARG -1
#define ESP_ERR_NOT_SUPPORTED -2
#define TAG "test"
#define CALL_VIDEO_SOURCE_CROP_X 0U
#define CALL_VIDEO_SOURCE_CROP_Y 0U
#define CALL_VIDEO_SOURCE_CROP_WIDTH 640U
#define CALL_VIDEO_SOURCE_CROP_HEIGHT 480U
#define CALL_VIDEO_DECODE_MAX_WIDTH 640U
#define CALL_VIDEO_DECODE_MAX_HEIGHT 480U
#define ESP_RETURN_ON_FALSE(condition, error, tag, message) \
    do { (void)(tag); (void)(message); if (!(condition)) return (error); } while (0)
typedef int esp_err_t;
'''
code += function(renderer, "static esp_err_t call_video_copy_display_i420(")
code += function(converter, "static void video_frame_fit_inside(")
code += r'''
static void verify_copy(uint16_t width, uint16_t height) {
    size_t bytes=(size_t)width*height*3U/2U;
    uint8_t *source=malloc(bytes), *output=malloc((size_t)640U*480U*3U/2U);
    assert(source && output);
    for(size_t i=0;i<bytes;++i) source[i]=(uint8_t)(i*17U+3U);
    memset(output,0,(size_t)640U*480U*3U/2U);
    assert(call_video_copy_display_i420(source,width,height,output)==ESP_OK);
    assert(memcmp(source,output,bytes)==0);
    free(source); free(output);
}
int main(void) {
    verify_copy(384,256);
    verify_copy(640,480);
    uint16_t width,height,x,y;
    video_frame_fit_inside(640,480,640,384,false,&width,&height,&x,&y);
    assert(width==512 && height==384 && x==64 && y==0);
    video_frame_fit_inside(384,256,640,384,false,&width,&height,&x,&y);
    assert(width==576 && height==384 && x==32 && y==0);
    return 0;
}
'''

with tempfile.TemporaryDirectory(prefix="call-video-fit-") as directory:
    path = Path(directory)
    (path / "test.c").write_text(code)
    subprocess.run(["cc", "-std=c11", "-Wall", "-Wextra", "-Werror",
                    str(path / "test.c"), "-o", str(path / "test")], check=True)
    subprocess.run([str(path / "test")], check=True)

assert "#define CALL_VIDEO_H264_FIT_COVER            0U" in config
assert ".source_crop_width = 0" in renderer
assert ".source_crop_height = 0" in renderer
assert ".source_max_width = CALL_VIDEO_DECODE_MAX_WIDTH" in renderer
assert ".source_max_height = CALL_VIDEO_DECODE_MAX_HEIGHT" in renderer
assert ".require_ppa = true" in renderer
assert "config->source_max_width" in converter
assert "config->source_max_height" in converter
assert "PPA I420 staging buffer is too small" in converter
assert "if (handle->config.require_ppa) return ppa_ret;" in converter
assert "video_frame_converter_i420_to_rgb565(converter" in renderer
assert "s_renderer.presentation.rotation" in renderer
assert "VIDEO_FRAME_ROTATION_CLOCKWISE_0" not in renderer[
    renderer.index("video_frame_converter_i420_to_rgb565(converter"):
    renderer.index("video_frame_converter_i420_to_rgb565(converter") + 500
]
assert "slot->width = resolution.width;" in renderer
assert "slot->height = resolution.height;" in renderer
print("PASS: device-call H264 preserves source pixels and contains each resolution centrally")
