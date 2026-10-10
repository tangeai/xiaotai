"""Run the production P4 presentation tick against timed frame arrival."""
import unittest
from test_s3_product_regressions import ROOT, function, run_c
SOURCE=ROOT/'firmware/esp-idf/esp32p4/waveshare-touch-lcd-43c/components/p4_hardware/p4_video.c'
class StaleVideo(unittest.TestCase):
 def test_missing_video_hides_old_frame_and_fresh_frame_restores_it(self):
  s=SOURCE.read_text()
  helpers=function(s,'hide_video_image') if 'static void hide_video_image(' in s else ''
  run_c(r'''
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include <stdatomic.h>
#define ESP_OK 0
#define LV_OBJ_FLAG_HIDDEN 1
#define LV_OBJ_FLAG_CLICKABLE 2
#define LV_IMG_CF_TRUE_COLOR 3
#define CALL_VIDEO_RENDER_WIDTH 2
#define CALL_VIDEO_RENDER_HEIGHT 2
#define VIDEO_STALE_US 3000000
#define ESP_LOGI(...) ((void)0)
typedef struct {bool hidden;} lv_obj_t;
typedef struct {struct {unsigned cf,w,h;} header;size_t data_size;const uint8_t *data;} lv_img_dsc_t;
static lv_obj_t image,screen,*s_image;
static lv_img_dsc_t s_image_desc;
static bool s_presented_frame_held,available;
static uint32_t s_presented_generation;
static int64_t s_last_presented_us,clock_us=100;
static atomic_uint s_live_generation=7;
static unsigned releases;
static uint16_t pixels_buf[4];
static int64_t esp_timer_get_time(void){return clock_us;}
static void lv_obj_add_flag(lv_obj_t *o,unsigned flag){if(flag==1)o->hidden=true;}
static void lv_obj_clear_flag(lv_obj_t *o,unsigned flag){if(flag==1)o->hidden=false;}
static void lv_img_cache_invalidate_src(void*p){(void)p;}
static void lv_obj_center(void*p){(void)p;}
static void lv_obj_move_background(void*p){(void)p;}
static void lv_obj_invalidate(void*p){(void)p;}
static lv_obj_t *lv_img_create(void*p){(void)p;return &image;}
static void lv_img_set_src(void*p,void*d){(void)p;(void)d;}
static void call_video_renderer_release_presented_rgb565(void){releases++;}
static int call_video_renderer_present_next_rgb565(const uint16_t**p,size_t*n,void*a,void*b){
 (void)a;(void)b;if(!available)return -1;available=false;*p=pixels_buf;*n=4;return 0;}
'''+helpers+function(s,'p4_video_ui_reset')+function(s,'p4_video_ui_tick')+r'''
int main(void){
 (void)s_presented_generation;(void)s_last_presented_us;(void)esp_timer_get_time();
 p4_video_ui_tick(&screen,true);assert(!s_image);
 available=true;p4_video_ui_tick(&screen,true);assert(s_image&&!image.hidden&&s_presented_frame_held);
 clock_us+=2999999;p4_video_ui_tick(&screen,true);assert(!image.hidden&&releases==0);
 clock_us++;p4_video_ui_tick(&screen,true);assert(image.hidden&&!s_presented_frame_held&&releases==1);
 p4_video_ui_tick(&screen,true);assert(releases==1);
 available=true;p4_video_ui_tick(&screen,true);assert(!image.hidden&&s_presented_frame_held);
 s_live_generation=8;p4_video_ui_tick(&screen,true);assert(image.hidden&&!s_presented_frame_held&&releases==2);
 available=true;p4_video_ui_tick(&screen,true);assert(!image.hidden);
 p4_video_ui_tick(&screen,false);assert(image.hidden&&releases==3);
 p4_video_ui_reset();assert(!s_image&&!s_presented_frame_held);
}
''')
if __name__=='__main__':unittest.main()
