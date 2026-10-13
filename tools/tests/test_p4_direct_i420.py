#!/usr/bin/env python3
"""Replay the production public converter at the official imgfx/PPA boundary."""
from pathlib import Path
import subprocess
import tempfile
root = Path(__file__).resolve().parents[2]
src = (root / 'platforms/esp-idf/waveshare_p4/video_frame_converter.c').read_text()
def function(signature):
    start = src.index(signature)
    return src[start:src.index('\n}', start)+2]
helper = function('static esp_err_t video_frame_convert_i420_direct(') if 'static esp_err_t video_frame_convert_i420_direct(' in src else ''
qvga_helper = function('static esp_err_t video_frame_convert_i420_qvga_imgfx(')
code = r'''
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
typedef int esp_err_t;
#define ESP_OK 0
#define ESP_FAIL -1
#define ESP_ERR_NOT_SUPPORTED 1
#define ESP_ERR_INVALID_ARG 2
#define ESP_ERR_NO_MEM 3
#define TAG "test"
#define ESP_RETURN_ON_FALSE(c,e,...) do{if(!(c))return e;}while(0)
#define ESP_RETURN_ON_ERROR(c,...) do{int r=(c);if(r)return r;}while(0)
#define ESP_LOGW(...) ((void)0)
#define ESP_LOGI(...) ((void)0)
#define taskENTER_CRITICAL(x) ((void)(x))
#define taskEXIT_CRITICAL(x) ((void)(x))
typedef int video_frame_rotation_t;
#define VIDEO_FRAME_ROTATION_CLOCKWISE_0 0
#define VIDEO_FRAME_FIT_CONTAIN 0
typedef enum {VIDEO_FRAME_CONVERTER_MODE_SOFTWARE,VIDEO_FRAME_CONVERTER_MODE_PPA,VIDEO_FRAME_CONVERTER_MODE_IMGFX} video_frame_converter_mode_t;
typedef struct {int width,height;} esp_imgfx_resolution_t;
typedef struct {esp_imgfx_resolution_t in_res;int in_pixel_fmt,out_pixel_fmt,color_space_std;} esp_imgfx_color_convert_cfg_t;
typedef struct {uint8_t *data;size_t data_len;} esp_imgfx_data_t;
typedef void *esp_imgfx_color_convert_handle_t;
#define ESP_IMGFX_PIXEL_FMT_I420 1
#define ESP_IMGFX_PIXEL_FMT_RGB565_LE 2
#define ESP_IMGFX_PIXEL_FMT_RGB565_BE 3
#define ESP_IMGFX_COLOR_SPACE_STD_BT601 0
#define ESP_IMGFX_ERR_OK 0
struct video_frame_converter {
 struct {bool prefer_direct_i420,prefer_native_qvga_i420,prevent_upscale,output_rgb565_byte_swap,require_ppa;uint16_t output_width,output_height;int fit_mode;} config;
 void *packed_yuv420;void *ppa_client;esp_imgfx_color_convert_handle_t imgfx,qvga_imgfx;uint8_t *qvga_rgb565;int stats_lock;
 struct {unsigned ppa_failures,software_frames,qvga_imgfx_frames;uint64_t software_time_us,qvga_color_time_us,qvga_clear_time_us,qvga_copy_time_us;uint32_t software_max_us,qvga_color_max_us,qvga_clear_max_us,qvga_copy_max_us;} stats;
 video_frame_converter_mode_t last_mode;bool imgfx_timing_logged,qvga_timing_logged,qvga_imgfx_unavailable;
};
typedef struct video_frame_converter *video_frame_converter_handle_t;
static int opens,processes,ppa_calls,software_calls,closes,open_fail,process_fail,byte_swap;
static int margin_clears;
static int64_t esp_timer_get_time(void){static int64_t t;t+=10;return t;}
static int esp_imgfx_color_convert_open(esp_imgfx_color_convert_cfg_t *cfg,void **h){opens++;assert((cfg->in_res.width==640&&cfg->in_res.height==480)||(cfg->in_res.width==320&&cfg->in_res.height==240));assert(cfg->in_pixel_fmt==ESP_IMGFX_PIXEL_FMT_I420);assert(cfg->out_pixel_fmt==(byte_swap?ESP_IMGFX_PIXEL_FMT_RGB565_BE:ESP_IMGFX_PIXEL_FMT_RGB565_LE));assert(cfg->color_space_std==ESP_IMGFX_COLOR_SPACE_STD_BT601);if(open_fail)return -1;*h=(void*)1;return 0;}
static int esp_imgfx_color_convert_process(void *h,esp_imgfx_data_t *in,esp_imgfx_data_t *out){assert(h);processes++;assert((in->data_len==460800&&out->data_len==614400)||(in->data_len==115200&&out->data_len==153600));if(process_fail)return -1;memset(out->data,0x5a,out->data_len);out->data[0]=byte_swap?0xf8:0;out->data[1]=byte_swap?0:0xf8;return 0;}
static int esp_imgfx_color_convert_close(void *h){assert(h);closes++;return 0;}
static bool video_frame_rotation_is_valid(int r){return r==0||r==90;}
static int video_frame_resolve_crop(void *h,unsigned w,unsigned ht,uint16_t *x,uint16_t *y,uint16_t *cw,uint16_t *ch){(void)h;*x=*y=0;*cw=w;*ch=ht;return 0;}
static int video_frame_apply_fit_crop(void *h,int r,bool b,unsigned w,unsigned ht,uint16_t*x,uint16_t*y,uint16_t*cw,uint16_t*ch){(void)h;(void)r;(void)b;(void)w;(void)ht;(void)x;(void)y;(void)cw;(void)ch;return 0;}
static void video_frame_record_render_layout(void*h,unsigned w,unsigned ht,unsigned x,unsigned y){(void)h;assert((w==640&&ht==480&&x==0&&y==0)||(w==320&&ht==240&&x==160&&y==120));}
static void video_frame_clear_margins(uint16_t*o,uint16_t w,uint16_t h,uint16_t x,uint16_t y,uint16_t rw,uint16_t rh){margin_clears++;assert(w==640&&h==480&&x==160&&y==120&&rw==320&&rh==240);memset(o,0,(size_t)y*w*2);for(unsigned row=y;row<y+rh;row++){memset(o+(size_t)row*w,0,x*2);memset(o+(size_t)row*w+x+rw,0,(w-x-rw)*2);}memset(o+(size_t)(y+rh)*w,0,(size_t)(h-y-rh)*w*2);}
static int video_frame_convert_yuv420_ppa(void*h,const void*i,unsigned w,unsigned ht,unsigned x,unsigned y,unsigned cw,unsigned ch,bool b,int r,void*o){(void)h;(void)i;(void)w;(void)ht;(void)x;(void)y;(void)cw;(void)ch;(void)b;(void)r;(void)o;ppa_calls++;return 0;}
static int video_frame_convert_i420_region(void*h,const void*i,unsigned w,unsigned ht,unsigned x,unsigned y,unsigned cw,unsigned ch,void*o){(void)h;(void)i;(void)w;(void)ht;(void)x;(void)y;(void)cw;(void)ch;(void)o;return -1;}
static int video_frame_convert_i420_scaled(void*h,const void*i,unsigned w,unsigned ht,unsigned x,unsigned y,unsigned cw,unsigned ch,int r,void*o){struct video_frame_converter*c=h;(void)i;(void)x;(void)y;assert(c->config.prevent_upscale&&w==320&&ht==240&&cw==320&&ch==240&&(r==0||r==90));software_calls++;((uint16_t*)o)[0]=0;return 0;}
'''
code += helper + '\n' + qvga_helper + '\n' + function('esp_err_t video_frame_converter_i420_to_rgb565(')
code += r'''
static int ppa_unregister_client(void *p){assert(p);return 0;}
static void heap_caps_free(void *p){free(p);}
'''
code += function('void video_frame_converter_destroy(')
code += r'''
int main(void){
 uint8_t *in=aligned_alloc(64,460800);uint16_t *out=aligned_alloc(64,614400);assert(in&&out);
 for(byte_swap=0;byte_swap<2;byte_swap++){
  struct video_frame_converter h={.config={.prefer_direct_i420=true,.output_width=640,.output_height=480,.require_ppa=true,.output_rgb565_byte_swap=byte_swap},.ppa_client=(void*)2};
  int before=processes;video_frame_converter_mode_t mode;
  assert(video_frame_converter_i420_to_rgb565(&h,in,640,480,0,out,&mode)==0);
  assert(processes==before+1&&mode==VIDEO_FRAME_CONVERTER_MODE_IMGFX&&ppa_calls==0);
  assert(((uint8_t*)out)[0]==(byte_swap?0xf8:0));
  int opened=opens;
  assert(video_frame_converter_i420_to_rgb565(&h,in,640,480,0,out,&mode)==0&&opens==opened);
  assert(video_frame_converter_i420_to_rgb565(&h,in,640,480,90,out,&mode)==0&&mode==VIDEO_FRAME_CONVERTER_MODE_PPA);
  assert(video_frame_converter_i420_to_rgb565(&h,in,320,240,0,out,&mode)==0&&mode==VIDEO_FRAME_CONVERTER_MODE_PPA);
  h.config.prefer_direct_i420=false;
  assert(video_frame_converter_i420_to_rgb565(&h,in,640,480,0,out,&mode)==0&&mode==VIDEO_FRAME_CONVERTER_MODE_PPA);
  h.config.prefer_direct_i420=true;process_fail=1;
  assert(video_frame_converter_i420_to_rgb565(&h,in,640,480,0,out,&mode)==0&&mode==VIDEO_FRAME_CONVERTER_MODE_PPA);
  process_fail=0;int closed=closes;
  struct video_frame_converter *owned=malloc(sizeof(h));assert(owned);*owned=h;
  video_frame_converter_destroy(owned);assert(closes==closed+1);h.imgfx=NULL;
  open_fail=1;
  assert(video_frame_converter_i420_to_rgb565(&h,in,640,480,0,out,&mode)==0&&mode==VIDEO_FRAME_CONVERTER_MODE_PPA);
  open_fail=0;ppa_calls=0;
 }
 struct video_frame_converter q={.config={.prefer_native_qvga_i420=true,.prevent_upscale=true,.output_width=640,.output_height=480,.require_ppa=true,.output_rgb565_byte_swap=true},.ppa_client=(void*)2,.qvga_rgb565=malloc(153600)};
 assert(q.qvga_rgb565);
 video_frame_converter_mode_t qmode;
 int ppa_before=ppa_calls;
 int process_before=processes;
 memset(out,0xff,614400);
 assert(video_frame_converter_i420_to_rgb565(&q,in,320,240,0,out,&qmode)==0);
 assert(qmode==VIDEO_FRAME_CONVERTER_MODE_IMGFX&&processes==process_before+1&&software_calls==0&&ppa_calls==ppa_before);
 assert(out[120U*640U+160U]==(byte_swap?0x00f8:0xf800));
 assert(out[0]==0&&out[119U*640U+160U]==0&&out[360U*640U+160U]==0);
 assert(out[120U*640U+479U]==0x5a5a&&out[359U*640U+160U]==0x5a5a);
 int opened=opens;
 assert(video_frame_converter_i420_to_rgb565(&q,in,320,240,0,out,&qmode)==0);
 assert(qmode==VIDEO_FRAME_CONVERTER_MODE_IMGFX&&opens==opened&&processes==process_before+2);
 assert(margin_clears==2);
 assert(q.stats.qvga_imgfx_frames==2&&q.stats.qvga_color_time_us>0&&q.stats.qvga_clear_time_us>0&&q.stats.qvga_copy_time_us>0);
 process_fail=1;
 assert(video_frame_converter_i420_to_rgb565(&q,in,320,240,0,out,&qmode)==0);
 assert(qmode==VIDEO_FRAME_CONVERTER_MODE_SOFTWARE&&software_calls==1&&ppa_calls==ppa_before);
 process_fail=0;
 assert(video_frame_converter_i420_to_rgb565(&q,in,320,240,90,out,&qmode)==0);
 assert(qmode==VIDEO_FRAME_CONVERTER_MODE_SOFTWARE&&software_calls==2&&ppa_calls==ppa_before);
 if(q.qvga_imgfx)esp_imgfx_color_convert_close(q.qvga_imgfx);
 free(q.qvga_rgb565);
 video_frame_converter_destroy(NULL);free(in);free(out);return 0;
}
'''
with tempfile.TemporaryDirectory(prefix='p4-i420-direct-') as d:
 p=Path(d);(p/'test.c').write_text(code)
 subprocess.run(['cc','-std=c11','-Wall','-Wextra','-Werror','-Wno-unused-function','-Wno-unused-variable','-fsanitize=undefined',str(p/'test.c'),'-o',str(p/'test')],check=True)
 subprocess.run([str(p/'test')],check=True)
print('PASS: public VGA/QVGA I420 converter, native centering, imgfx reuse and failure fallback')
