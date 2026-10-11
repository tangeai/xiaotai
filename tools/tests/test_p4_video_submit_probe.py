"""Replay the diagnostic control and real encoded-frame callback boundary."""
import unittest
from test_s3_product_regressions import ROOT, function, run_c

class VideoSubmitProbe(unittest.TestCase):
    def test_pause_preserves_encoded_callback_and_resumes_at_keyframe(self):
        source=(ROOT/'platforms/esp-idf/components/starter_tirtc/src/starter_tirtc.c').read_text()
        names=('starter_tirtc_debug_video_submit_reset','starter_tirtc_debug_video_submit_pause','starter_tirtc_debug_video_submit_state','starter_tirtc_debug_video_submit_skipped','starter_tirtc_debug_video_submit_admit','clear_subscriptions')
        functions='\n'.join(function(source,n) for n in names)
        console=function((ROOT/'platforms/esp-idf/components/starter_console/src/starter_console.c').read_text(),'command_video_submit')
        callback=function((ROOT/'firmware/esp-idf/esp32p4/waveshare-touch-lcd-43c/components/p4_hardware/p4_video.c').read_text(),'send_video')
        run_c(r'''
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include <stdatomic.h>
#define CONFIG_STARTER_PIPELINE_DIAGNOSTICS 1
#define CONFIG_IDF_TARGET_ESP32P4 1
#define STARTER_TIRTC_CALL 4
#define STARTER_TIRTC_VOIP 3
#define ESP_OK 0
#define ESP_FAIL -1
#define ESP_ERR_INVALID_STATE -2
#define TIRTC_VIDEO_H264 66
typedef int esp_err_t;
static atomic_uint s_debug_video_submit_state, s_debug_video_submit_skipped;
static atomic_bool s_audio_subscribed, s_video_subscribed;
static atomic_uint s_live_generation=1, s_sent, s_tx_failed;
static atomic_int s_tx_last_error;
static atomic_bool s_tx_enabled=true, s_camera_enabled=true, s_video_send_blocked;
static unsigned mode=4, captures, submissions, key_requests;
static bool connected=true;
static int starter_tirtc_mode(void){return mode;}
static bool starter_tirtc_connected(void){return connected;}
static uint32_t starter_tirtc_generation(void){return 1;}
static bool starter_tirtc_video_ready(void){return true;}
static void p4_video_capture_offer(const uint8_t*d,size_t n,uint16_t w,uint16_t h,bool key,uint32_t g){assert(d&&n&&w==640&&h==480&&g==1);captures++;}
static int starter_tirtc_send_h264(uint32_t ts,const void*d,uint32_t n,bool key){assert(ts==20&&d&&n);submissions++;return 123;}
static void camera_pipeline_request_key_frame(void){key_requests++;}
'''+functions+'\n'+callback+'\n'+console+r'''
int main(void){
 char *bad[]={"video-submit","bad"}, *pause[]={"video-submit","pause"}, *resume[]={"video-submit","resume"}, *show[]={"video-submit","show"};
 assert(command_video_submit(1,bad)==1&&command_video_submit(2,bad)==1);
 assert(command_video_submit(2,show)==0);
 const uint8_t frame[8]={0};
 assert(starter_tirtc_debug_video_submit_state()==0);
 assert(send_video(frame,8,640,480,20000,66,true,0)==0&&submissions==1);
 assert(command_video_submit(2,pause)==0);
 for(unsigned i=0;i<50;i++)assert(send_video(frame,8,640,480,20000,66,i%10==0,0)==0);
 assert(captures==51&&submissions==1&&s_sent==1&&!s_tx_failed&&s_tx_enabled&&s_camera_enabled);
 assert(starter_tirtc_debug_video_submit_skipped()==50);
 assert(command_video_submit(2,resume)==0);
 assert(starter_tirtc_debug_video_submit_state()==2);
 assert(starter_tirtc_debug_video_submit_pause(false));
 assert(send_video(frame,8,640,480,20000,66,false,0)==0&&submissions==1);
 assert(send_video(frame,8,640,480,20000,66,true,0)==0&&submissions==2);
 assert(starter_tirtc_debug_video_submit_state()==0&&s_sent==2&&!key_requests);
 assert(starter_tirtc_debug_video_submit_pause(false)&&starter_tirtc_debug_video_submit_state()==0);
 assert(starter_tirtc_debug_video_submit_pause(true));
 clear_subscriptions();
 assert(starter_tirtc_debug_video_submit_state()==0&&!starter_tirtc_debug_video_submit_skipped());
 connected=false;assert(!starter_tirtc_debug_video_submit_pause(true));
 connected=true;mode=3;assert(!starter_tirtc_debug_video_submit_pause(true));
 assert(starter_tirtc_debug_video_submit_admit(false));
}
''')

    def test_compile_out_cannot_pause_video(self):
        source=(ROOT/'platforms/esp-idf/components/starter_tirtc/src/starter_tirtc.c').read_text()
        names=('starter_tirtc_debug_video_submit_reset','starter_tirtc_debug_video_submit_pause','starter_tirtc_debug_video_submit_state','starter_tirtc_debug_video_submit_skipped','starter_tirtc_debug_video_submit_admit')
        run_c("#include <assert.h>\n#include <stdbool.h>\n#define CONFIG_STARTER_PIPELINE_DIAGNOSTICS 0\n#define CONFIG_IDF_TARGET_ESP32P4 1\n"+'\n'.join(function(source,n) for n in names)+r'''
int main(void){starter_tirtc_debug_video_submit_reset();assert(!starter_tirtc_debug_video_submit_pause(true));assert(!starter_tirtc_debug_video_submit_state());assert(!starter_tirtc_debug_video_submit_skipped());assert(starter_tirtc_debug_video_submit_admit(false));}
''')

if __name__=='__main__':unittest.main()
