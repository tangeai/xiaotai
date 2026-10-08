"""Exercise the production H5 camera loop through board and TiRTC seams."""
import unittest
from test_s3_product_regressions import MEDIA, function, run_c

class S3H5Cadence(unittest.TestCase):
    def test_encoder_within_budget_reaches_eight_fps_without_starving_idle(self):
        run_c(r'''
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include <stdatomic.h>
#include <setjmp.h>
#define ESP_OK 0
#define ESP_LOGW(...) ((void)0)
#define pdMS_TO_TICKS(x) (x)
#define STARTER_TIRTC_H5 1
#define XIAOTAI_CAMERA_FRAME_RGB565 1
#define PIXFORMAT_RGB565 1
#define CAMERA_FRAME_INTERVAL_MS 125U
#define VIDEO_BACKPRESSURE_BYTES 196608U
#define JPEG_BUFFER_BYTES 131072U
#define CAMERA_JPEG_QUALITY 80U
#define MEDIA_CPU_YIELD_MS 1U
typedef unsigned TickType_t;
typedef int starter_tirtc_mode_t;
typedef struct {const void *data;size_t size,width,height;int format;} xiaotai_board_camera_frame_t;
typedef struct {uint8_t *buf;size_t len,width,height;int format;} camera_fb_t;
typedef struct {uint8_t *buffer;size_t capacity,length;bool overflow;} starter_jpeg_collector_t;
static atomic_uint s_mode,s_generation,s_video_sent,s_jpeg_deadline_misses;
static atomic_bool s_video_refresh_requested;
static atomic_uint_fast32_t s_jpeg_max_encode_us;
static uint8_t s_jpeg_buffer[JPEG_BUFFER_BYTES];
static uint32_t now_ms,encoding_ms,acquired,released,sends,yields,last_timestamp;
static jmp_buf done;
static int64_t esp_timer_get_time(void){return (int64_t)now_ms*1000;}
static void vTaskDelay(unsigned ticks){assert(ticks>0);now_ms+=ticks;yields++;}
static bool same_session(int m,uint32_t g){return m==1 && g==7;}
static bool starter_tirtc_video_ready(void){return true;}
static size_t starter_tirtc_send_buffer_used(void){return 0;}
static bool xiaotai_board_camera_frame_is_valid(const xiaotai_board_camera_frame_t *f){return true;}
static int acquire(void *ctx,xiaotai_board_camera_frame_t *f){
    acquired++;*f=(xiaotai_board_camera_frame_t){.data=s_jpeg_buffer,.size=153600,.width=320,.height=240,.format=1};return 0;
}
static void release(void *ctx,xiaotai_board_camera_frame_t *f){released++;}
static struct {void *context;int (*acquire)(void *,xiaotai_board_camera_frame_t *);
    void (*release)(void *,xiaotai_board_camera_frame_t *);} adapter={NULL,acquire,release},*s_camera_adapter=&adapter;
static void starter_jpeg_collect(void){}
static bool frame2jpg_cb(camera_fb_t *f,unsigned quality,void (*cb)(void),starter_jpeg_collector_t *jpg){
    assert(f->width==320 && f->height==240 && quality==80);
    now_ms+=encoding_ms;jpg->length=100;return true;
}
static void update_max_counter(atomic_uint_fast32_t *p,uint32_t v){if(v>*p)*p=v;}
static int starter_tirtc_send_mjpeg(uint32_t timestamp,const void *data,size_t size){
    if(sends){
        uint32_t expected=encoding_ms<125 ? 125 : encoding_ms+1;
        assert(timestamp-last_timestamp==expected && "H5 scheduling must not oversleep its 125ms frame budget");
    }
    last_timestamp=timestamp;sends++;
    if(sends==20)longjmp(done,1);
    return 0;
}
''' + function(MEDIA.read_text(),'camera_task') + r'''
int main(void){
    s_mode=1;s_generation=7;
    const unsigned durations[]={90,110,124,200};
    for(unsigned i=0;i<4;i++){
        now_ms=0;acquired=released=sends=yields=0;encoding_ms=durations[i];
        s_jpeg_deadline_misses=0;
        if(!setjmp(done))camera_task(NULL);
        assert(acquired==20 && released==20 && yields>=20);
        assert(s_jpeg_deadline_misses==(encoding_ms>125 ? 20U : 0U));
    }
}
''')

if __name__ == '__main__':
    unittest.main()
