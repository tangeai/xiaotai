#!/usr/bin/env python3
"""Exercise the public packed camera-to-JPEG RGB565 conversion boundary."""
from pathlib import Path
import os
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[2]
source = (ROOT / "platforms/esp-idf/waveshare_p4/video_frame_converter.c").read_text()


def function(signature):
    start = source.index(signature)
    return source[start:source.index("\n}", start) + 2]


code = r'''
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>
typedef int esp_err_t;
#define ESP_OK 0
#define ESP_FAIL -1
#define ESP_ERR_NOT_SUPPORTED -2
#define ESP_ERR_INVALID_ARG -3
#define ESP_CACHE_MSYNC_FLAG_DIR_M2C 1
#define TAG "test"
#define ESP_RETURN_ON_FALSE(c,e,...) do { if (!(c)) return (e); } while (0)
#define ESP_RETURN_ON_ERROR(c,...) do { int r=(c); if (r) return r; } while (0)
#define taskENTER_CRITICAL(x) ((void)(x))
#define taskEXIT_CRITICAL(x) ((void)(x))
typedef enum {VIDEO_FRAME_CONVERTER_MODE_SOFTWARE,VIDEO_FRAME_CONVERTER_MODE_PPA} video_frame_converter_mode_t;
typedef struct video_frame_converter *video_frame_converter_handle_t;
typedef struct video_frame_converter {
 struct {unsigned output_width,output_height;bool output_rgb565_byte_swap,prefer_packed_half_rgb565;} config;
 void *ppa_client;int stats_lock;
 int32_t luma_terms[256];
 struct {unsigned ppa_failures,software_frames;uint64_t software_time_us;unsigned software_max_us;} stats;
 video_frame_converter_mode_t last_mode;
} video_frame_converter;
#define VIDEO_FRAME_ROTATION_CLOCKWISE_0 0
static int ppa_calls, sync_calls, fail_sync;
static const void *expected_input;
static size_t expected_size;
static int64_t esp_timer_get_time(void) { static int64_t t; t+=10; return t; }
static int esp_cache_msync(void *p,size_t n,unsigned flags) {
 sync_calls++; assert(p==expected_input&&n==expected_size&&flags==ESP_CACHE_MSYNC_FLAG_DIR_M2C);
 return fail_sync ? ESP_FAIL : ESP_OK;
}
static uint8_t video_frame_clip_u8(int v) {return v<0?0:v>255?255:v;}
static uint16_t video_frame_yuv_terms_to_rgb565(int l,int r,int g,int b,bool swap) {
 uint8_t rr=video_frame_clip_u8((l+r)>>8),gg=video_frame_clip_u8((l+g)>>8),bb=video_frame_clip_u8((l+b)>>8);
 uint16_t rgb=(uint16_t)(((rr&0xf8)<<8)|((gg&0xfc)<<3)|(bb>>3));
 return swap?(uint16_t)((rgb>>8)|(rgb<<8)):rgb;
}
static int video_frame_resolve_crop(void *h,unsigned w,unsigned ht,uint16_t *x,uint16_t *y,uint16_t *cw,uint16_t *ch) {
 (void)h;*x=*y=0;*cw=w;*ch=ht;return ESP_OK;
}
static int video_frame_apply_fit_crop(void *h,int r,bool b,unsigned w,unsigned ht,uint16_t*x,uint16_t*y,uint16_t*cw,uint16_t*ch) {
 (void)h;(void)r;(void)b;(void)w;(void)ht;(void)x;(void)y;(void)cw;(void)ch;return ESP_OK;
}
static int video_frame_convert_yuv420_ppa(void*h,const uint8_t*i,unsigned w,unsigned ht,unsigned x,unsigned y,unsigned cw,unsigned ch,bool b,int r,uint16_t*o) {
 (void)h;(void)i;(void)w;(void)ht;(void)x;(void)y;(void)cw;(void)ch;(void)b;(void)r;(void)o;ppa_calls++;return ESP_OK;
}
static void video_frame_record_render_layout(void*h,unsigned w,unsigned ht,unsigned x,unsigned y) {
 (void)h;assert(w==16&&ht==8&&x==0&&y==0);
}
'''
helper_name = "static esp_err_t video_frame_convert_ouyy_evyy_half_rgb565("
if helper_name in source:
    code += function(helper_name) + "\n"
code += function("esp_err_t video_frame_converter_ouyy_evyy_to_rgb565(")
code += r'''
static uint16_t reference_pixel(const uint8_t *in,unsigned x,unsigned y,unsigned ow,bool swap) {
 unsigned stride=ow*3,off=(2*y)*stride+3*x;
 const uint8_t *a=in+off,*b=a+stride;
 unsigned luma=(a[1]+a[2]+b[1]+b[2]+2)/4;
 int c=(int)luma-16,u=(int)a[0]-128,v=(int)b[0]-128;
 if(c<0)c=0;
 return video_frame_yuv_terms_to_rgb565(298*c,409*v+128,-100*u-208*v+128,516*u+128,swap);
}
int main(void) {
 enum {OW=16,OH=8, IW=32,IH=16,IN_SIZE=IW*IH*3/2,OUT_SIZE=OW*OH};
 uint8_t *raw=malloc(IN_SIZE+16);uint16_t *dst=malloc(OUT_SIZE*2+16);assert(raw&&dst);
 uint8_t *in=raw+8;uint16_t *out=(uint16_t*)((uint8_t*)dst+8);
 memset(raw,0xa5,IN_SIZE+16);memset(dst,0xa5,OUT_SIZE*2+16);
 for(unsigned y=0;y<IH;y++)for(unsigned x=0;x<IW;x+=2) {
  unsigned i=y*(IW*3/2)+x*3/2;
  in[i]=(uint8_t)((y&1)? 96+x*3:56+y*5);
  in[i+1]=(uint8_t)(16+(x+y*3)%220);
  in[i+2]=(uint8_t)(16+(x*2+y*7)%220);
 }
 video_frame_converter h={.config={.output_width=OW,.output_height=OH,.prefer_packed_half_rgb565=true}};
 for(unsigned i=0;i<256;i++)h.luma_terms[i]=298*(i>16?(int)i-16:0);
 expected_input=in;expected_size=IN_SIZE;
 for(int swap=0;swap<2;swap++) {
  h.config.output_rgb565_byte_swap=swap;video_frame_converter_mode_t mode=VIDEO_FRAME_CONVERTER_MODE_PPA;
  assert(video_frame_converter_ouyy_evyy_to_rgb565(&h,in,IW,IH,out,&mode)==ESP_OK);
  assert(mode==VIDEO_FRAME_CONVERTER_MODE_SOFTWARE&&ppa_calls==0);
  for(unsigned y=0;y<OH;y++)for(unsigned x=0;x<OW;x++)assert(out[y*OW+x]==reference_pixel(in,x,y,OW,swap));
 }
 assert(sync_calls==2&&h.stats.software_frames==2);
 for(unsigned i=0;i<8;i++)assert(raw[i]==0xa5&&raw[8+IN_SIZE+i]==0xa5);
 for(unsigned i=0;i<8;i++)assert(((uint8_t*)dst)[i]==0xa5&&((uint8_t*)dst)[8+OUT_SIZE*2+i]==0xa5);
 fail_sync=1;
 assert(video_frame_converter_ouyy_evyy_to_rgb565(&h,in,IW,IH,out,NULL)==ESP_FAIL&&ppa_calls==0);
 fail_sync=0;h.ppa_client=(void*)1;
 assert(video_frame_converter_ouyy_evyy_to_rgb565(&h,in,IW+2,IH,out,NULL)==ESP_OK&&ppa_calls==1);
 h.config.prefer_packed_half_rgb565=false;
 assert(video_frame_converter_ouyy_evyy_to_rgb565(&h,in,IW,IH,out,NULL)==ESP_OK&&ppa_calls==2);
 free(raw);free(dst);return 0;
}
'''
with tempfile.TemporaryDirectory(prefix="p4-packed-rgb565-") as directory:
    path = Path(directory)
    (path / "test.c").write_text(code)
    subprocess.run(["cc", "-std=c11", "-O2", "-Wall", "-Wextra", "-Werror",
                    "-Wno-unused-function", "-fsanitize=address,undefined", "-fno-sanitize-recover=all",
                    str(path / "test.c"), "-o", str(path / "test")], check=True)
    subprocess.run([str(path / "test")], check=True,
                   env=dict(os.environ, ASAN_OPTIONS="detect_leaks=0"))
print("PASS: public packed YUV420 half-scale RGB565 pixels, guards, cache and fallback")
