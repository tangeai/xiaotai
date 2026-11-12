"""Video ingress allocation must survive failed initialization and retry."""
import unittest
from test_s3_product_regressions import ROOT,function,run_c

class VideoInitRetry(unittest.TestCase):
    def test_each_ingress_failure_rolls_back_and_success_is_reused(self):
        s=(ROOT/'firmware/esp-idf/esp32p4/waveshare-touch-lcd-43c/components/p4_hardware/p4_video.c').read_text()
        run_c(r'''
#include <assert.h>
#include <stddef.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
typedef int esp_err_t;
#define ESP_OK 0
#define ESP_ERR_NO_MEM -1
#define ESP_LOGW(...) ((void)0)
#define VIDEO_INGRESS_SLOTS 3
#define VIDEO_INGRESS_BYTES 100
#define MALLOC_CAP_SPIRAM 1
#define MALLOC_CAP_8BIT 2
static bool s_initialized;
static void *s_free,*s_pending;
static struct {void*data;} s_ingress[3];
static unsigned calls,fail_at,live;
static void*alloc(void){if(++calls==fail_at)return NULL;live++;return malloc(8);}
static void release(void*p){if(p){assert(live);live--;free(p);}}
#define xQueueCreate(...) alloc()
#define heap_caps_malloc(...) alloc()
#define xQueueSend(...) 1
#define vQueueDelete(p) release(p)
#define heap_caps_free(p) release(p)
#define p4_board_probe_register() 0
#define call_video_renderer_prewarm_mjpeg_decoder() 0
#define camera_pipeline_prewarm_h264() 0
#define camera_pipeline_prewarm_call_scaler() 0
#define call_video_renderer_prewarm() 0
#define camera_pipeline_set_rtc_video_sink(...) 0
#define p4_video_capture_init() (++calls==fail_at?-2:0)
''' + function(s,'p4_video_init') + r'''
int main(void){
 for(fail_at=1;fail_at<=6;fail_at++){
  calls=0;assert(p4_video_init()!=0);assert(live==0 && !s_initialized);
 }
 fail_at=0;assert(p4_video_init()==0);unsigned before=calls;
 assert(p4_video_init()==0 && calls==before && live==5);
}
''')
if __name__=='__main__':unittest.main()
