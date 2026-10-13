#!/usr/bin/env python3
"""Check the public packed-camera to JPEG UYVY half-scale boundary."""
from pathlib import Path
import os
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]
SOURCE = ROOT / "platforms/esp-idf/waveshare_p4/video_frame_converter.c"
HEADER = ROOT / "platforms/esp-idf/waveshare_p4/video_frame_converter.h"
CAMERA = ROOT / "firmware/esp-idf/esp32p4/waveshare-touch-lcd-43c/main/media/camera_pipeline.c"


class PackedMjpegUyvy(unittest.TestCase):
    def test_full_frame_half_scale_and_bounds(self):
        source = SOURCE.read_text()
        signature = "esp_err_t video_frame_converter_ouyy_evyy_half_to_uyvy("
        self.assertIn(signature, HEADER.read_text())
        self.assertIn(signature, source)
        start = source.index(signature)
        implementation = source[start:source.index("\n}", start) + 2]
        code = r'''
#include <assert.h>
#include <stdint.h>
#include <stddef.h>
#include <string.h>
typedef int esp_err_t;
#define ESP_OK 0
#define ESP_FAIL -1
#define ESP_ERR_INVALID_ARG -3
#define ESP_CACHE_MSYNC_FLAG_DIR_M2C 1
#define TAG "test"
#define ESP_RETURN_ON_FALSE(c,e,...) do {if (!(c)) return (e);} while (0)
#define ESP_RETURN_ON_ERROR(c,...) do {esp_err_t r=(c);if(r) return r;} while (0)
static const void *expected_input;
static size_t expected_size;
static int sync_calls, fail_sync;
static int esp_cache_msync(void *p,size_t n,unsigned flags) {
    sync_calls++;assert(p==expected_input&&n==expected_size&&flags==ESP_CACHE_MSYNC_FLAG_DIR_M2C);
    return fail_sync ? ESP_FAIL : ESP_OK;
}
''' + implementation + r'''
int main(void) {
    enum {OW=16,OH=8,IW=32,IH=16,IN_SIZE=IW*IH*3/2,OUT_SIZE=OW*OH*2};
    uint8_t raw[IN_SIZE+16],dst[OUT_SIZE+16];
    uint8_t *in=raw+8,*out=dst+8;
    memset(raw,0xa5,sizeof(raw));memset(dst,0xa5,sizeof(dst));
    for(unsigned y=0;y<IH;y++)for(unsigned x=0;x<IW;x+=2) {
        unsigned i=y*(IW*3/2)+x*3/2;
        in[i]=(uint8_t)((y&1)?96+x:56+x);
        in[i+1]=(uint8_t)(16+(x+y*3)%220);
        in[i+2]=(uint8_t)(16+(x*2+y*7)%220);
    }
    expected_input=in;expected_size=IN_SIZE;
    assert(video_frame_converter_ouyy_evyy_half_to_uyvy(in,IW,IH,out,OW,OH)==ESP_OK);
    assert(sync_calls==1);
    for(unsigned y=0;y<OH;y++)for(unsigned x=0;x<OW;x+=2) {
        unsigned stride=IW*3/2;
        const uint8_t *a=in+(2*y)*stride+3*x,*b=a+stride;
        unsigned y0=(a[1]+a[2]+b[1]+b[2]+2)/4;
        unsigned y1=(a[4]+a[5]+b[4]+b[5]+2)/4;
        unsigned u=(a[0]+a[3]+1)/2,v=(b[0]+b[3]+1)/2;
        unsigned i=(y*OW+x)*2;
        assert(out[i]==u&&out[i+1]==y0&&out[i+2]==v&&out[i+3]==y1);
    }
    for(unsigned i=0;i<8;i++)assert(raw[i]==0xa5&&raw[8+IN_SIZE+i]==0xa5&&dst[i]==0xa5&&dst[8+OUT_SIZE+i]==0xa5);
    assert(video_frame_converter_ouyy_evyy_half_to_uyvy(in,IW,IH,out,OW-1,OH)==ESP_ERR_INVALID_ARG);
    assert(video_frame_converter_ouyy_evyy_half_to_uyvy(in,IW,IH,out,OW,OH-1)==ESP_ERR_INVALID_ARG);
    assert(video_frame_converter_ouyy_evyy_half_to_uyvy(in,IW,IH,out,OW+2,OH)==ESP_ERR_INVALID_ARG);
    fail_sync=1;
    assert(video_frame_converter_ouyy_evyy_half_to_uyvy(in,IW,IH,out,OW,OH)==ESP_FAIL);
    return 0;
}
'''
        with tempfile.TemporaryDirectory(prefix="p4-mjpeg-uyvy-") as directory:
            path = Path(directory)
            (path / "test.c").write_text(code)
            subprocess.run(["cc", "-std=c11", "-O2", "-Wall", "-Wextra", "-Werror",
                            "-fsanitize=address,undefined", "-fno-sanitize-recover=all",
                            str(path / "test.c"), "-o", str(path / "test")], check=True)
            subprocess.run([str(path / "test")], check=True,
                           env=dict(os.environ, ASAN_OPTIONS="detect_leaks=0"))

    def test_call_uses_jpeg_yuv422_and_voip_stays_h264(self):
        source = CAMERA.read_text()
        start = source.index("static void camera_pipeline_run_call_mjpeg(void)")
        worker = source[start:source.index("\n}\n", start) + 2]
        self.assertIn("JPEG_ENCODE_IN_FORMAT_YUV422", worker)
        # IDF 5.5.5 has a nonzero DMA2D hb entry only for YUV422 -> YUV422.
        # YUV422 -> YUV420 passes its API validator but times out on P4.
        self.assertIn(".sub_sample = JPEG_DOWN_SAMPLING_YUV422", worker)
        self.assertIn("video_frame_converter_ouyy_evyy_half_to_uyvy(", worker)
        self.assertNotIn("JPEG_ENCODE_IN_FORMAT_RGB565", worker)
        self.assertIn("TIRTC_VIDEO_JPEG", worker)
        self.assertIn("camera_pipeline_run_call_mjpeg();", source)


if __name__ == "__main__":
    unittest.main()
