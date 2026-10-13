#!/usr/bin/env python3
"""Exercise the idle camera snapshot through its real driver/scaler calls."""
from pathlib import Path
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]
SOURCE = ROOT / "platforms/esp-idf/waveshare_p4/p4_video_capture.c"


class CameraSnapshot(unittest.TestCase):
    def test_capture_full_and_native_vga(self):
        source = SOURCE.read_text()
        start = source.index("static esp_err_t camera_snapshot_capture(")
        body = source[start:source.index("\n}", start) + 2]
        for native in (0, 1):
            code = r'''
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include <string.h>
#define ESP_OK 0
#define ESP_FAIL -1
#define ESP_ERR_INVALID_ARG -2
#define ESP_ERR_INVALID_STATE -3
#define ESP_ERR_INVALID_SIZE -4
#define ESP_CACHE_MSYNC_FLAG_DIR_M2C 8
#define CAMERA_DRIVER_PIXEL_FORMAT_YUV420_OUYY_EVYY 2
#define APP_MEDIA_CALL_VIDEO_WIDTH 640
#define APP_MEDIA_CALL_VIDEO_HEIGHT 480
#define APP_MEDIA_CALL_VIDEO_FPS 8
#define APP_MEDIA_CAMERA_CAPTURE_WIDTH 1280
#define APP_MEDIA_CAMERA_CAPTURE_HEIGHT 960
#define pdMS_TO_TICKS(ms) (ms)
typedef int esp_err_t;
typedef struct {const uint8_t *data;size_t data_len;uint16_t width,height;int pixel_format;} camera_driver_frame_t;
typedef struct {uint16_t input_width,input_height,output_width,output_height;bool rotate_ccw90,fit_contain;} video_yuv420_scaler_config_t;
typedef void *video_yuv420_scaler_handle_t;
static uint8_t frame_data[1280*960*3/2];
static uint8_t scaled[640*480*3/2];
static int active,connected,acquired,released,frame_released,created,destroyed,synced,captures,delays;
static uint16_t target_w,target_h;
static int camera_pipeline_is_running(void){return active;}
static int starter_tirtc_connected(void){return connected;}
static int camera_driver_set_stream_target(uint16_t w,uint16_t h,uint8_t fps){
 target_w=w;target_h=h;assert(fps==8);return ESP_OK;
}
static int camera_driver_acquire(void){acquired++;return ESP_OK;}
static void camera_driver_release_device(void){released++;}
static int camera_driver_capture(camera_driver_frame_t *f){
 captures++;
 *f=(camera_driver_frame_t){frame_data,(size_t)target_w*target_h*3/2,target_w,target_h,
  CAMERA_DRIVER_PIXEL_FORMAT_YUV420_OUYY_EVYY};return ESP_OK;
}
static void vTaskDelay(int ticks){assert(ticks==250);delays++;}
static void camera_driver_release(camera_driver_frame_t *f){(void)f;frame_released++;}
static int esp_cache_msync(void *p,size_t n,int flags){
 assert(p==frame_data&&n==sizeof(scaled)&&flags==ESP_CACHE_MSYNC_FLAG_DIR_M2C);
 synced++;return ESP_OK;
}
static int video_yuv420_scaler_create(const video_yuv420_scaler_config_t *c,void **h){
 assert(c->input_width==1280&&c->input_height==960&&c->output_width==640&&c->output_height==480);
 assert(!c->rotate_ccw90&&c->fit_contain);created++;*h=scaled;return ESP_OK;
}
static int video_yuv420_scaler_process(void *h,const uint8_t *in,size_t len,
 const uint8_t **out,size_t *outlen){
 assert(h==scaled&&in==frame_data&&len==sizeof(frame_data));
 *out=scaled;*outlen=sizeof(scaled);return ESP_OK;
}
static void video_yuv420_scaler_destroy(void *h){if(h){assert(h==scaled);destroyed++;}}
'''
            code += body
            code += r'''
int main(void){
 (void)vTaskDelay;
 uint8_t out[640*480*3/2];uint16_t w=0,h=0;
 memset(frame_data,0x5a,sizeof(frame_data));memset(scaled,0x31,sizeof(scaled));
 active=1;assert(camera_snapshot_capture(out,sizeof(out),APP_MEDIA_CALL_NATIVE_VGA_CAPTURE_ENABLE,&w,&h)==ESP_ERR_INVALID_STATE);
 assert(!acquired&&!released);
 active=0;connected=1;assert(camera_snapshot_capture(out,sizeof(out),APP_MEDIA_CALL_NATIVE_VGA_CAPTURE_ENABLE,&w,&h)==ESP_ERR_INVALID_STATE);
 connected=0;assert(camera_snapshot_capture(out,sizeof(out)-1,APP_MEDIA_CALL_NATIVE_VGA_CAPTURE_ENABLE,&w,&h)==ESP_ERR_INVALID_SIZE);
 assert(camera_snapshot_capture(out,sizeof(out),APP_MEDIA_CALL_NATIVE_VGA_CAPTURE_ENABLE,&w,&h)==ESP_OK);
 assert(w==640&&h==480&&acquired==1&&released==1&&captures==5&&frame_released==5&&delays==4);
 for(size_t i=0;i<sizeof(out);i++)assert(out[i]==(APP_MEDIA_CALL_NATIVE_VGA_CAPTURE_ENABLE?0x5a:0x31));
 assert(created==!APP_MEDIA_CALL_NATIVE_VGA_CAPTURE_ENABLE);
 assert(destroyed==created&&synced==APP_MEDIA_CALL_NATIVE_VGA_CAPTURE_ENABLE);
 assert(camera_snapshot_capture(out,sizeof(out),!APP_MEDIA_CALL_NATIVE_VGA_CAPTURE_ENABLE,&w,&h)==ESP_OK);
 assert(acquired==2&&released==2&&captures==10&&frame_released==10&&delays==8);
 for(size_t i=0;i<sizeof(out);i++)assert(out[i]==(APP_MEDIA_CALL_NATIVE_VGA_CAPTURE_ENABLE?0x31:0x5a));
 assert(created==1&&destroyed==1&&synced==1);
 return 0;
}
'''
            with tempfile.TemporaryDirectory(prefix="p4-snapshot-") as temp:
                path = Path(temp)
                (path / "test.c").write_text(code)
                exe = path / "test"
                subprocess.run(["cc", "-std=c11", "-O2", "-Wall", "-Wextra", "-Werror",
                                f"-DAPP_MEDIA_CALL_NATIVE_VGA_CAPTURE_ENABLE={native}",
                                str(path / "test.c"), "-o", str(exe)], check=True)
                subprocess.run([str(exe)], check=True)


if __name__ == "__main__":
    unittest.main()
