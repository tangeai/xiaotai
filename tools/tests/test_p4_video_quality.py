"""Run the media owner's two-tier controller with actual governor profiles."""
import unittest
from test_s3_product_regressions import ROOT, function, run_c
P4=ROOT/'firmware/esp-idf/esp32p4/waveshare-touch-lcd-43c'
class VideoQuality(unittest.TestCase):
 def test_congestion_recovery_and_session_boundaries(self):
  s=(P4/'components/p4_hardware/p4_video.c').read_text()
  g=(P4/'main/media/media_governor.c').read_text()
  self.assertIn('if (maintain_call_quality()) return;',function(s,'maintain_bitrate'))
  run_c(r'''
#include <assert.h>
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include <string.h>
#include <stdatomic.h>
#include "'''+str(P4/'main/media/media_tuning.h')+r'''"
#define ESP_OK 0
#define ESP_LOGI(...) ((void)0)
#define ESP_LOGW(...) ((void)0)
#define STARTER_TIRTC_CALL 4
#define STARTER_TIRTC_VOIP 3
#define CONFIG_APP_RTC_VIDEO_AUTO_ADAPT_ENABLE 0
#define MEDIA_GOVERNOR_WEAK_NETWORK_OFF 0
#define MEDIA_GOVERNOR_WEAK_NETWORK_RESOLUTION_PRIORITY 2
typedef struct {uint16_t width,height;uint8_t fps;uint32_t bitrate_bps;int weak_network_mode;uint8_t weak_network_level,h264_min_qp,h264_max_qp;} media_governor_video_config_t;
static unsigned generation=1,mode=4,changes,keys,configs;
static uint32_t feedback;static size_t used;static int64_t now=1;
static atomic_uint s_live_generation=1,s_tx_failed=0;
static atomic_bool s_tx_enabled=true,s_camera_enabled=true;
static uint32_t starter_tirtc_generation(void){return generation;}
static int starter_tirtc_mode(void){return mode;}
static int64_t esp_timer_get_time(void){return now;}
static size_t starter_tirtc_send_buffer_used(void){return used;}
static int __attribute__((unused)) starter_tirtc_set_video_bitrate_range(uint32_t a,uint32_t b,uint32_t c){(void)a;(void)b;(void)c;configs++;assert(!"CALL/VOIP must not enable SDK bitrate range");return 0;}
static bool __attribute__((unused)) starter_tirtc_take_video_bitrate(uint32_t gen,uint32_t*b){(void)gen;(void)b;assert(!"local-only controller must ignore SDK suggestions");return false;}
static media_governor_video_config_t current;
static int media_governor_set_rtc_video_config(const media_governor_video_config_t*c){current=*c;return 0;}
static void camera_pipeline_on_rtc_video_config_changed(void){changes++;}
static void camera_pipeline_request_stream_start_key_frame(void){keys++;}
'''+function(g,'media_governor_build_device_call_video_config')+function(g,'media_governor_build_wechat_video_config')+function(g,'media_governor_build_call_quality_config')+s[s.index('typedef struct { /* Two-tier'):s.index('static bool maintain_call_quality')]+function(s,'maintain_call_quality')+r'''
static void poll(int64_t delta){now+=delta;assert(maintain_call_quality());}
int main(void){
 assert(maintain_call_quality());assert(current.width==640&&current.height==480&&current.fps==5&&current.bitrate_bps==150000&&configs==0);
 used=70000;poll(500000);assert(current.width==640);poll(1000000);assert(current.width==640);poll(500000);assert(current.width==320&&current.height==240&&current.fps==5&&current.bitrate_bps==96000);
 used=0;poll(500000);poll(29000000);assert(current.width==320);poll(1000000);assert(current.width==640);assert(keys==changes);
 /* A single pressure sample cannot force a downgrade. */
 used=70000;poll(500000);used=0;poll(500000);assert(current.width==640);
 /* SDK suggestions cannot force a downgrade on an otherwise healthy link. */
 feedback=100000;poll(500000);poll(1500000);assert(current.width==640&&feedback==100000);
 /* Failure protection and VoIP reset do not enable SDK pacing either. */
 mode=3;generation=2;s_live_generation=2;poll(500000);assert(current.width==640&&current.fps==5&&configs==0);
 s_tx_enabled=false;s_tx_failed++;poll(500000);poll(1500000);assert(current.width==320);
 /* Privacy suspends the controller and resets timers before re-enabling. */
 s_tx_enabled=false;s_camera_enabled=false;now+=60000000;assert(maintain_call_quality());assert(current.width==320);
 s_tx_enabled=true;s_camera_enabled=true;poll(500000);assert(current.width==320);poll(30000000);assert(current.width==640);
 /* Stale generations and unrelated modes cannot mutate the profile. */
 unsigned n=changes;s_live_generation=1;assert(maintain_call_quality());assert(changes==n);
 s_live_generation=2;mode=1;assert(!maintain_call_quality());assert(changes==n);
 generation=3;s_live_generation=3;mode=4;poll(500000);assert(current.width==640&&current.fps==5&&configs==0);
}
''')
 def test_encoder_can_return_to_vga_after_qvga(self):
  s=(P4/'main/media/camera_pipeline.c').read_text()
  run_c(r'''
#include <assert.h>
#include <stdint.h>
'''+function(s,'camera_pipeline_h264_target_dimensions')+r'''
int main(void){uint16_t w,h;
 camera_pipeline_h264_target_dimensions(320,240,640,480,0,0,&w,&h);assert(w==320&&h==240);
 camera_pipeline_h264_target_dimensions(640,480,640,480,0,0,&w,&h);assert(w==640&&h==480);
 camera_pipeline_h264_target_dimensions(640,480,640,480,320,240,&w,&h);assert(w==320&&h==240);
 camera_pipeline_h264_target_dimensions(640,480,320,240,0,0,&w,&h);assert(w==320&&h==240);
}
''')
  worker=function(s,'camera_pipeline_task')
  self.assertIn('camera_pipeline_h264_target_dimensions(',worker)
  self.assertNotIn('target_width = h264.width;',worker)
  self.assertIn('next_h264_open_tick = 0;\n            stream_start_key_frame_requested = false;',worker)
  self.assertIn('camera_pipeline_h264_force_next_idr(&h264, \"stream-start\")',worker)
if __name__=='__main__':unittest.main()
