"""Exercise packed YUV420 pixels and cache ownership through the public scaler."""
from pathlib import Path
import subprocess
import os
import tempfile
import unittest

from test_s3_product_regressions import ROOT, function


class UplinkHalfScale(unittest.TestCase):
    def test_full_frame_pixels_guards_cache_errors_and_ppa_fallback(self):
        source = (ROOT / "platforms/esp-idf/waveshare_p4/video_yuv420_scaler.c").read_text()
        code = r'''
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#define ESP_OK 0
#define ESP_FAIL -1
#define ESP_ERR_INVALID_ARG -2
#define ESP_ERR_INVALID_SIZE -3
#define ESP_CACHE_MSYNC_FLAG_DIR_M2C 1
#define ESP_CACHE_MSYNC_FLAG_DIR_C2M 2
#define ESP_CACHE_MSYNC_FLAG_UNALIGNED 4
#define VIDEO_YUV420_SCALE_DENOMINATOR 16U
#define PPA_SRM_COLOR_MODE_YUV420 1
#define PPA_SRM_ROTATION_ANGLE_0 0
#define PPA_SRM_ROTATION_ANGLE_90 1
#define PPA_TRANS_MODE_BLOCKING 0
typedef int esp_err_t;
typedef void *ppa_client_handle_t;
typedef struct {uint16_t input_width,input_height,output_width,output_height; bool rotate_ccw90,fit_contain;} video_yuv420_scaler_config_t;
typedef struct {const void *buffer; size_t buffer_size; unsigned pic_w,pic_h,block_w,block_h,block_offset_x,block_offset_y,srm_cm;} picture_t;
typedef struct {picture_t in,out; unsigned rotation_angle,mode; float scale_x,scale_y;} ppa_srm_oper_config_t;
static unsigned ppa_calls,sync_calls,fail_sync;
static const void *expected_input,*expected_output;
static size_t expected_in_len,expected_out_len;
static int ppa_do_scale_rotate_mirror(void *client,const ppa_srm_oper_config_t *op){
 (void)client;ppa_calls++;
 if(op->out.pic_w==320){
  assert(op->scale_x==0.25f);
  assert(op->scale_y==0.25f&&op->in.pic_w==1280&&op->in.pic_h==960);
  assert(op->out.pic_w==320&&op->out.pic_h==240);
  assert(op->in.block_w==1280&&op->in.block_h==960);
  assert(op->in.block_offset_x==0&&op->in.block_offset_y==0);
 }
 return ESP_FAIL;
}
static int __attribute__((unused)) esp_cache_msync(void *p,size_t len,unsigned flags){
 sync_calls++;
 if(sync_calls&1){assert(p==expected_input&&len==expected_in_len);assert(flags==ESP_CACHE_MSYNC_FLAG_DIR_M2C);}
 else {assert(p==expected_output&&len==expected_out_len);assert(flags==(ESP_CACHE_MSYNC_FLAG_DIR_C2M|ESP_CACHE_MSYNC_FLAG_UNALIGNED));}
 return sync_calls==fail_sync?ESP_FAIL:ESP_OK;
}
'''
        start = source.index("struct video_yuv420_scaler {")
        code += source[start:source.index("\n};", start) + 3]
        code += "\ntypedef struct video_yuv420_scaler *video_yuv420_scaler_handle_t;\n"
        code += function(source, "video_yuv420_data_size")
        for name in ("video_yuv420_use_cpu_half_scale", "video_yuv420_half_scale_ouyy_evyy"):
            if name in source:
                code += function(source, name)
        code += function(source, "video_yuv420_scaler_process")
        code += r'''
static uint8_t luma(unsigned y,unsigned x){return (uint8_t)(3*x+7*y+x*y%17);}
static uint8_t chroma(bool v,unsigned y,unsigned x){return (uint8_t)((v?173:29)+11*x+19*y+x*y%13);}
static void exercise(unsigned w,unsigned h,unsigned offset){
 unsigned ow=w/2,oh=h/2;size_t in_len=w*h*3/2,out_len=ow*oh*3/2;
 uint8_t *a=malloc(in_len+8),*b=malloc(out_len+8);assert(a&&b);
 memset(a,0xa5,in_len+8);memset(b,0xa5,out_len+8);
 uint8_t *input=a+offset,*output=b+offset;
 for(unsigned y=0;y<h;y++)for(unsigned x=0;x<w;x+=2){
  size_t pos=(y*w+x)*3/2;
  input[pos]=chroma(y&1,y/2,x/2);input[pos+1]=luma(y,x);input[pos+2]=luma(y,x+1);
 }
 struct video_yuv420_scaler scaler={.config={w,h,ow,oh,false,true},.crop_width=w,.crop_height=h,
  .render_width=ow,.render_height=oh,.scale_step=8,.output_buffer=output,.output_buffer_size=out_len,.output_data_len=out_len};
 const uint8_t *result=NULL;size_t result_len=0;
 sync_calls=fail_sync=ppa_calls=0;expected_input=input;expected_output=output;expected_in_len=in_len;expected_out_len=out_len;
 assert(video_yuv420_scaler_process(&scaler,input,in_len,&result,&result_len)==ESP_OK);
 assert(result==output&&result_len==out_len&&ppa_calls==0&&sync_calls==2);
 for(unsigned y=0;y<oh;y++)for(unsigned x=0;x<ow;x+=2){
  unsigned uv=0,ys[2]={0,0};
  for(unsigned dy=0;dy<2;dy++)for(unsigned dx=0;dx<2;dx++){
   uv+=chroma(y&1,(y/2)*2+dy,x+dx);
   ys[0]+=luma(2*y+dy,2*x+dx);ys[1]+=luma(2*y+dy,2*(x+1)+dx);
  }
  size_t pos=(y*ow+x)*3/2;
  assert(output[pos]==(uv+2)/4&&output[pos+1]==(ys[0]+2)/4&&output[pos+2]==(ys[1]+2)/4);
 }
 for(unsigned i=0;i<offset;i++)assert(b[i]==0xa5&&a[i]==0xa5);
 for(size_t i=offset+out_len;i<out_len+8;i++)assert(b[i]==0xa5);
 for(size_t i=offset+in_len;i<in_len+8;i++)assert(a[i]==0xa5);
 for(unsigned failure=1;failure<=2;failure++){
  sync_calls=0;fail_sync=failure;result=NULL;result_len=0;
  assert(video_yuv420_scaler_process(&scaler,input,in_len,&result,&result_len)==ESP_FAIL);
  assert(!result&&!result_len&&ppa_calls==0&&sync_calls==failure);
 }
 fail_sync=sync_calls=0;
 assert(video_yuv420_scaler_process(&scaler,input,in_len-1,&result,&result_len)==ESP_ERR_INVALID_SIZE&&sync_calls==0);
 assert(video_yuv420_scaler_process(NULL,input,in_len,&result,&result_len)==ESP_ERR_INVALID_ARG);
 scaler.config.rotate_ccw90=true;
 assert(video_yuv420_scaler_process(&scaler,input,in_len,&result,&result_len)==ESP_FAIL&&ppa_calls==1);
 scaler.config.rotate_ccw90=false;scaler.crop_x=2;
 assert(video_yuv420_scaler_process(&scaler,input,in_len,&result,&result_len)==ESP_FAIL&&ppa_calls==2);
 scaler.crop_x=0;scaler.scale_step=4;
 assert(video_yuv420_scaler_process(&scaler,input,in_len,&result,&result_len)==ESP_FAIL&&ppa_calls==3);
 free(a);free(b);
}
int main(void){
 exercise(1280,960,0);
 uint8_t *input=calloc(1,1280*960*3/2),*output=calloc(1,320*240*3/2);
 assert(input&&output);
 struct video_yuv420_scaler quarter={.config={1280,960,320,240,false,true},
  .crop_width=1280,.crop_height=960,.scale_step=4,.render_width=320,.render_height=240,
  .output_buffer=output,.output_buffer_size=320*240*3/2,.output_data_len=320*240*3/2,
  .ppa_client=(void*)1};
 const uint8_t *result=NULL;size_t result_len=0;ppa_calls=sync_calls=0;
 assert(video_yuv420_scaler_process(&quarter,input,1280*960*3/2,&result,&result_len)==ESP_FAIL);
 assert(ppa_calls==1&&sync_calls==0&&result==NULL&&result_len==0);
 free(input);free(output);
}
'''
        with tempfile.TemporaryDirectory(prefix="p4-half-scale-") as directory:
            path = Path(directory)
            (path / "test.c").write_text(code)
            subprocess.run(["cc", "-std=c11", "-O2", "-Wall", "-Wextra", "-Werror",
                            "-fsanitize=address,undefined", "-fno-sanitize-recover=all",
                            str(path / "test.c"), "-o", str(path / "test")], check=True)
            subprocess.run([str(path / "test")], check=True,
                           env=dict(os.environ, ASAN_OPTIONS="detect_leaks=0"))


if __name__ == "__main__":
    unittest.main()
