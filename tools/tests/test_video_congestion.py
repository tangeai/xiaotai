"""Replay SDK queue saturation and generation-scoped bitrate suggestions."""
import unittest
from test_s3_product_regressions import ROOT, function, run_c
from test_s3_voip_audio import COMMON
SOURCE=ROOT/'platforms/esp-idf/components/starter_tirtc/src/starter_tirtc.c'
class VideoCongestion(unittest.TestCase):
 def test_video_leaves_audio_capacity(self):
  s=SOURCE.read_text()
  run_c(COMMON+r'''
#define AI_AUDIO_STREAM 1
#define VOIP_AUDIO_STREAM 0
#define ROOM_AUDIO_STREAM 1
#define CALL_AUDIO_STREAM 10
#define TIRTC_E_BUSY -9
#define TIRTC_VIDEO_H264 9
#define TIRTC_FRAME_FLAG_KEY_FRAME 1
static atomic_uint s_send_buffer_limit=2097152;
static size_t used;static unsigned video_sends;
static size_t TiRtcGetSendBufferUsed(void *c){return used;}
static bool starter_tirtc_video_ready(void){return true;}
static unsigned video_stream_for_mode(int mode){return mode==3?1:11;}
static int TiRtcSendVideoStream(void*c,TIRTCFRAMEINFO*f,const void*d){video_sends++;used+=f->length;return 0;}
''' + function(s,'audio_stream_for_mode') + function(s,'starter_tirtc_send_h264') + function(s,'starter_tirtc_send_alaw') + r'''
int main(void){current_mode=3;expected=0;
 used=2097152-65536-100;assert(starter_tirtc_send_h264(20,"v",100,true)==0);
 assert(video_sends==1 && used==2097152-65536);
 assert(starter_tirtc_send_h264(20,"v",1,true)==TIRTC_E_BUSY && video_sends==1);
 assert(starter_tirtc_send_alaw(20,"a",160)==0);
 used=2097152;assert(starter_tirtc_send_h264(20,"v",100,false)==TIRTC_E_BUSY);
 used=100;assert(starter_tirtc_send_h264(20,"v",100,true)==0);
 s_send_buffer_limit=1024;used=800;assert(starter_tirtc_send_h264(20,"v",200,true)==TIRTC_E_BUSY);
}
''')
 def test_sdk_bitrate_is_a_bounded_generation_scoped_mailbox(self):
  s=SOURCE.read_text()
  self.assertIn('.on_update_bitrate = on_update_bitrate', s)
  run_c(COMMON+r'''
static atomic_uint s_active_generation=7;
static int s_bitrate_lock;
#define taskENTER_CRITICAL(x) ((void)(x))
#define taskEXIT_CRITICAL(x) ((void)(x))
static uint32_t s_bitrate_generation, s_bitrate_target;
static unsigned video_stream_for_mode(int mode){return mode==3?1:11;}
''' + function(s,'on_update_bitrate') + function(s,'starter_tirtc_take_video_bitrate') + r'''
int main(void){uint32_t target=0;current_mode=3;
 on_update_bitrate((void*)2,1,900000);assert(!starter_tirtc_take_video_bitrate(7,&target));
 on_update_bitrate((void*)1,11,900000);assert(!starter_tirtc_take_video_bitrate(7,&target));
 on_update_bitrate((void*)1,1,900000);on_update_bitrate((void*)1,1,300000);
 assert(!starter_tirtc_take_video_bitrate(6,&target));
 assert(starter_tirtc_take_video_bitrate(7,&target)&&target==300000);
 assert(!starter_tirtc_take_video_bitrate(7,&target));
 on_update_bitrate((void*)1,1,500000);s_active_generation=8;
 assert(!starter_tirtc_take_video_bitrate(8,&target));
 on_update_bitrate((void*)1,1,0);assert(!starter_tirtc_take_video_bitrate(8,&target));
 current_mode=2;on_update_bitrate((void*)1,11,500000);assert(!starter_tirtc_take_video_bitrate(8,&target));
}
''')
 def test_bitrate_range_uses_current_video_stream(self):
  s=SOURCE.read_text()
  run_c(COMMON+r'''
static unsigned configurations;
static unsigned video_stream_for_mode(int mode){return mode==3?1:11;}
static int TiRtcConnSetVideoBitrateParams(void*c,unsigned stream,uint32_t a,uint32_t b,uint32_t d){
 assert(c==(void*)1&&stream==expected&&a==200000&&b==1500000&&d==1200000);configurations++;return -42;
}
'''+function(s,'starter_tirtc_set_video_bitrate_range')+r'''
int main(void){int modes[]={1,3,4};
 for(unsigned i=0;i<3;i++){current_mode=modes[i];expected=current_mode==3?1:11;
 assert(starter_tirtc_set_video_bitrate_range(200000,1500000,1200000)==-42);}
 assert(configurations==3);
 assert(starter_tirtc_set_video_bitrate_range(0,1500000,1200000)<0);
 assert(starter_tirtc_set_video_bitrate_range(200000,1000000,1200000)<0);
 current_mode=2;assert(starter_tirtc_set_video_bitrate_range(200000,1500000,1200000)<0);
 current_mode=3;s_connection=0;assert(starter_tirtc_set_video_bitrate_range(200000,1500000,1200000)<0);
 assert(configurations==3);}
''')
 def test_owner_combines_sdk_and_queue_pressure(self):
  s=(ROOT/'firmware/esp-idf/esp32p4/waveshare-touch-lcd-43c/components/p4_hardware/p4_video.c').read_text()
  self.assertIn('maintain_bitrate();', function(s, 'p4_video_poll'))
  run_c(COMMON+r'''
#define ESP_OK 0
static atomic_uint s_live_generation=7;
static atomic_bool s_tx_enabled=true;
static uint32_t s_bitrate_session,s_sdk_bitrate;
static bool s_bitrate_configured;
static bool s_bitrate_feedback_enabled;
static int64_t s_bitrate_due_us,clock_us=1;
static uint32_t suggestion,applied,current=1500000;
static size_t used,limit=2097152;
static unsigned configured,changes,calls;
static int config_result;
static bool fixed_probe;
/* Non-call modes retain the generic bitrate controller. */
static bool maintain_call_quality(void){return false;}
typedef struct {uint32_t min_bitrate_bps,max_bitrate_bps,start_bitrate_bps;} media_governor_transport_bitrate_range_t;
typedef struct {uint32_t bitrate_bps;} media_governor_video_config_t;
static uint32_t starter_tirtc_generation(void){return 7;}
static int64_t esp_timer_get_time(void){return clock_us;}
static void media_governor_get_transport_bitrate_range(media_governor_transport_bitrate_range_t*r){*r=fixed_probe?(media_governor_transport_bitrate_range_t){150000,150000,150000}:(media_governor_transport_bitrate_range_t){250000,1500000,1200000};}
static int starter_tirtc_set_video_bitrate_range(uint32_t a,uint32_t b,uint32_t c){assert(a==250000&&b==1500000&&c==1200000);configured++;return config_result;}
static bool starter_tirtc_take_video_bitrate(uint32_t g,uint32_t*t){assert(g==7);if(!suggestion)return false;*t=suggestion;suggestion=0;return true;}
static size_t starter_tirtc_send_buffer_used(void){return used;}
static size_t starter_tirtc_send_buffer_limit(void){return limit;}
static void media_governor_get_rtc_video_config(media_governor_video_config_t*c){c->bitrate_bps=current;}
static int media_governor_apply_transport_bitrate_target(uint32_t t,bool*c){applied=t;calls++;*c=t!=current;return 0;}
static void camera_pipeline_on_rtc_video_config_changed(void){changes++;}
'''+function(s,'maintain_bitrate')+r'''
int main(void){
 (void)s_bitrate_feedback_enabled;
 maintain_bitrate();assert(configured==1&&applied==1200000);
 suggestion=300000;clock_us+=500000;maintain_bitrate();assert(applied==300000&&changes==2);
 suggestion=1500000;used=1500000;clock_us+=500000;maintain_bitrate();assert(applied==1125000);
 current=1000000;used=400000;clock_us+=500000;maintain_bitrate();assert(applied==1000000);
 used=0;clock_us+=500000;maintain_bitrate();assert(applied==1500000&&configured==1);
 unsigned n=calls;s_tx_enabled=false;clock_us+=500000;maintain_bitrate();assert(calls==n);
 s_tx_enabled=true;s_live_generation=6;maintain_bitrate();assert(calls==n);
 /* Reproduce WHIP/TGMP incompatibility: local audio protection must still run. */
 s_live_generation=7;s_bitrate_session=0;config_result=TIRTC_E_INVALID_PARAMETER;
 used=1500000;current=1500000;clock_us+=500000;maintain_bitrate();
 assert(applied==1125000 && calls==n+1 && configured==2);
 clock_us+=500000;maintain_bitrate();assert(configured==2 && calls==n+2);
 used=0;clock_us+=500000;maintain_bitrate();assert(applied==1500000 && configured==2);
 /* Unexpected SDK errors also fall back without retrying on every poll. */
 s_bitrate_session=0;config_result=-40013;used=1500000;clock_us+=500000;
 maintain_bitrate();assert(applied==1125000 && configured==3);
 clock_us+=500000;maintain_bitrate();assert(configured==3);
 /* Fixed call experiment never invokes the strict min < start < max SDK API,
  * consumes no SDK feedback, and cannot change target even under backlog. */
 fixed_probe=true;s_bitrate_session=0;current=150000;used=limit;suggestion=600000;
 n=calls;clock_us+=500000;maintain_bitrate();
 assert(configured==3&&calls==n&&suggestion==600000);
 clock_us+=500000;maintain_bitrate();assert(calls==n&&configured==3);
 /* A later adaptive session must re-enable its original controller. */
 fixed_probe=false;s_bitrate_session=0;suggestion=300000;used=0;config_result=0;
 clock_us+=500000;maintain_bitrate();assert(configured==4&&applied==300000);
}
''')
if __name__=='__main__':unittest.main()
