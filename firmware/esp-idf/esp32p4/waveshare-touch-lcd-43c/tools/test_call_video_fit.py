#!/usr/bin/env python3
"""Device-call downlink preserves the decoded frame and contains it centrally."""
from pathlib import Path
import subprocess
import tempfile
import re

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
#define ESP_ERR_INVALID_SIZE -3
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
typedef int video_frame_rotation_t;
#define VIDEO_FRAME_ROTATION_CLOCKWISE_0 0
#define VIDEO_FRAME_ROTATION_CLOCKWISE_90 90
#define VIDEO_FRAME_ROTATION_CLOCKWISE_180 180
#define VIDEO_FRAME_ROTATION_CLOCKWISE_270 270
struct video_frame_converter {
    struct {uint16_t output_width,output_height;bool prevent_upscale,output_rgb565_byte_swap;} config;
    int32_t luma_terms[256];
    uint16_t render_width,render_height,offset_x,offset_y;
};
typedef struct video_frame_converter *video_frame_converter_handle_t;
static bool video_frame_rotation_swaps_axes(int rotation) {return rotation==90||rotation==270;}
static void video_frame_record_render_layout(video_frame_converter_handle_t h,
    uint16_t w,uint16_t ht,uint16_t x,uint16_t y) {
    h->render_width=w;h->render_height=ht;h->offset_x=x;h->offset_y=y;
}
static uint16_t video_frame_yuv_to_rgb565(const int32_t *terms,int y,int u,int v,bool swap) {
    (void)terms;(void)u;(void)v;(void)swap;return (uint16_t)y;
}
'''
code += function(converter, "static esp_err_t video_frame_convert_i420_scaled(")
for name in ("CALL_VIDEO_RENDER_WIDTH", "CALL_VIDEO_RENDER_HEIGHT"):
    value = re.search(r"#define\s+" + name + r"\s+(\d+)U", config).group(1)
    code += f"\n#define {name} {value}U\n"
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
static void verify_native_qvga(int rotation) {
    uint8_t *source=malloc(320U*240U*3U/2U);
    uint16_t *output=malloc(640U*480U*sizeof(uint16_t));
    assert(source&&output);
    for(unsigned y=0;y<240;++y) for(unsigned x=0;x<320;++x)
        source[y*320U+x]=(uint8_t)((x+3U*y)%255U+1U);
    memset(source+320U*240U,128,320U*240U/2U);
    struct video_frame_converter h={.config={.output_width=640,.output_height=480,.prevent_upscale=true}};
    assert(video_frame_convert_i420_scaled(&h,source,320,240,0,0,320,240,rotation,output)==ESP_OK);
    const unsigned rw=rotation==0?320U:240U, rh=rotation==0?240U:320U;
    const unsigned ox=rotation==0?160U:200U, oy=rotation==0?120U:80U;
    assert(h.render_width==rw&&h.render_height==rh&&h.offset_x==ox&&h.offset_y==oy);
    for(unsigned y=0;y<480;++y) for(unsigned x=0;x<640;++x) {
        uint16_t expected=0;
        if(x>=ox&&x<ox+rw&&y>=oy&&y<oy+rh) {
            unsigned sx=rotation==0?x-ox:y-oy;
            unsigned sy=rotation==0?y-oy:239U-(x-ox);
            expected=source[sy*320U+sx];
        }
        assert(output[y*640U+x]==expected);
    }
    free(source);free(output);
}
int main(void) {
    verify_copy(384,256);
    verify_copy(640,480);
    uint16_t width,height,x,y;
    video_frame_fit_inside(640,480,CALL_VIDEO_RENDER_WIDTH,CALL_VIDEO_RENDER_HEIGHT,
                           false,&width,&height,&x,&y);
    assert(width==640 && height==480 && x==0 && y==0);
    video_frame_fit_inside(480,640,CALL_VIDEO_RENDER_WIDTH,CALL_VIDEO_RENDER_HEIGHT,
                           false,&width,&height,&x,&y);
    assert(width==360 && height==480 && x==140 && y==0);
    video_frame_fit_inside(640,480,640,384,false,&width,&height,&x,&y);
    assert(width==512 && height==384 && x==64 && y==0);
    video_frame_fit_inside(384,256,640,384,false,&width,&height,&x,&y);
    assert(width==576 && height==384 && x==32 && y==0);
    video_frame_fit_inside(320,240,640,480,true,&width,&height,&x,&y);
    assert(width==320 && height==240 && x==160 && y==120);
    video_frame_fit_inside(240,320,640,480,true,&width,&height,&x,&y);
    assert(width==240 && height==320 && x==200 && y==80);
    verify_native_qvga(0);
    verify_native_qvga(90);
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
assert ".prefer_native_qvga_i420 = true" in renderer
assert ".prevent_upscale = true" in renderer
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
