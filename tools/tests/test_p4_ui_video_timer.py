"""Replay the production LVGL video timer across idle/voice/video transitions."""
import unittest
from test_s3_product_regressions import ROOT, function, run_c

PRODUCT=ROOT/'platforms/esp-idf/components/starter_product/src/starter_product.c'
VIDEO=ROOT/'firmware/esp-idf/esp32p4/waveshare-touch-lcd-43c/components/p4_hardware/p4_video.c'

class P4UiVideoTimer(unittest.TestCase):
    def test_polling_pauses_without_live_video_and_recovers(self):
        source=PRODUCT.read_text()
        helper=function(source,'product_video_timer_sync') if 'static void product_video_timer_sync(void)\n{' in source else 'static void product_video_timer_sync(void) {}'
        active=function(VIDEO.read_text(),'p4_video_ui_active') if 'bool p4_video_ui_active(void)' in VIDEO.read_text() else 'static bool p4_video_ui_active(void) { return atomic_load(&s_live_generation)!=0; }'
        run_c(r'''
#include <assert.h>
#include <stdbool.h>
#include <stdatomic.h>
#include <stddef.h>
#define PAGE_CALL 5
#define STARTER_TIRTC_CALL 4
#define STARTER_TIRTC_VOIP 3
static int mode=4;
static int starter_tirtc_mode(void){return mode;}
static int s_page;
static atomic_uint s_live_generation;
typedef int starter_tirtc_mode_t;
typedef struct {bool paused;} lv_timer_t;
static lv_timer_t timer={true},*s_video_timer=&timer;
static bool s_video_timer_running;
static unsigned updates,clears,resumes,pauses,readies;
static void *lv_scr_act(void){return (void *)1;}
static void lv_timer_pause(lv_timer_t *t){assert(t==&timer);t->paused=true;pauses++;}
static void lv_timer_resume(lv_timer_t *t){assert(t==&timer);t->paused=false;resumes++;}
static void lv_timer_ready(lv_timer_t *t){assert(t==&timer);readies++;}
static void p4_video_ui_tick(void *screen,bool show){assert(screen==(void*)1);if(show && s_live_generation)updates++;else clears++;}
'''+active+helper+function(source,'product_video_tick')+r'''
int main(void){
 /* Idle/AI and voice-only calls must not repeatedly enter display cleanup. */
 for(unsigned i=0;i<100;i++){product_video_timer_sync();product_video_tick(&timer);}
 assert(updates==0 && clears==0 && resumes==0);
 s_page=PAGE_CALL;product_video_timer_sync();assert(timer.paused);
 s_live_generation=7;product_video_timer_sync();assert(!timer.paused && resumes==1 && readies==1);
 for(unsigned i=0;i<20;i++){product_video_timer_sync();product_video_tick(&timer);}
 assert(updates==20 && resumes==1 && clears==0);
 /* Media owner invalidation between 100 ms product ticks is handled immediately. */
 s_live_generation=0;product_video_tick(&timer);
 assert(timer.paused && pauses==1 && clears==1);
 product_video_timer_sync();product_video_tick(&timer);assert(clears==1);
 s_live_generation=8;product_video_timer_sync();product_video_tick(&timer);
 assert(updates==21 && resumes==2);
 s_page=0;product_video_timer_sync();assert(timer.paused && clears==2);
 for(unsigned i=0;i<100;i++){product_video_timer_sync();}
 assert(clears==2);
 /* H5 can own video while the home screen is shown: no local video polling. */
 assert(s_live_generation && timer.paused);
 s_page=PAGE_CALL;mode=1;product_video_timer_sync();
 assert(timer.paused && resumes==2); /* An incoming call page over H5 is not video-ready. */
 mode=3;product_video_timer_sync();assert(!timer.paused && resumes==3);
 (void)s_video_timer_running;(void)s_video_timer;
}
''')

if __name__=='__main__':unittest.main()
