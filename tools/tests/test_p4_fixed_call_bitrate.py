"""Exercise the actual governor entry/normalization and transport range for the call probe."""
import unittest
from test_s3_product_regressions import ROOT, function, run_c
P4=ROOT/'firmware/esp-idf/esp32p4/waveshare-touch-lcd-43c'
class FixedCallBitrate(unittest.TestCase):
 def test_call_starts_fixed_but_other_profiles_keep_adaptive_ranges(self):
  source=(P4/'main/media/media_governor.c').read_text()
  run_c(r'''
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include "'''+str(P4/'main/media/media_tuning.h')+r'''"
#define CONFIG_APP_RTC_VIDEO_AUTO_ADAPT_ENABLE 1
#define MEDIA_GOVERNOR_WEAK_NETWORK_OFF 0
#define MEDIA_GOVERNOR_WEAK_NETWORK_RESOLUTION_PRIORITY 2
#define MEDIA_GOVERNOR_VIDEO_MIN_BITRATE_BPS 200000U
#define MEDIA_GOVERNOR_TRANSPORT_COMPACT_MAX_PIXELS (640U*480U)
#define MEDIA_GOVERNOR_TRANSPORT_COMPACT_MIN_BITRATE_BPS APP_MEDIA_TGMP_COMPACT_MIN_BITRATE_BPS
#define MEDIA_GOVERNOR_TRANSPORT_LARGE_MIN_BITRATE_BPS APP_MEDIA_TGMP_LARGE_MIN_BITRATE_BPS
#define MEDIA_GOVERNOR_TRANSPORT_MIN_RATIO_DIVISOR APP_MEDIA_TGMP_MIN_RATIO_DIVISOR
#define MEDIA_GOVERNOR_TRANSPORT_START_RANGE_PERCENT APP_MEDIA_TGMP_START_RANGE_PERCENT
typedef struct {uint16_t width,height; uint8_t fps; uint32_t bitrate_bps; int weak_network_mode; uint8_t weak_network_level,h264_min_qp,h264_max_qp;} media_governor_video_config_t;
typedef struct {uint32_t min_bitrate_bps,max_bitrate_bps,start_bitrate_bps;} media_governor_transport_bitrate_range_t;
static media_governor_video_config_t s_rtc_video_config;
'''+function(source,'media_governor_normalize_video_config')+function(source,'media_governor_build_device_call_video_config')+function(source,'media_governor_build_transport_bitrate_range')+function(source,'media_governor_build_h5_video_config')+function(source,'media_governor_build_wechat_video_config')+r'''
int main(void){
 media_governor_video_config_t c; media_governor_transport_bitrate_range_t r;
 media_governor_build_device_call_video_config(&c);
 assert(c.width==640&&c.height==480&&c.fps==5&&c.bitrate_bps==150000);
 assert(c.weak_network_mode==MEDIA_GOVERNOR_WEAK_NETWORK_OFF);
 c=media_governor_normalize_video_config(&c);assert(c.bitrate_bps==150000);
 media_governor_build_transport_bitrate_range(&c,&r);
 assert(r.min_bitrate_bps==150000&&r.max_bitrate_bps==150000&&r.start_bitrate_bps==150000);
 media_governor_build_h5_video_config(&c);assert(c.bitrate_bps==3000000&&c.fps==20);
 media_governor_build_transport_bitrate_range(&c,&r);assert(r.min_bitrate_bps<r.start_bitrate_bps&&r.start_bitrate_bps<r.max_bitrate_bps);
 media_governor_build_wechat_video_config(&c);assert(c.bitrate_bps==500000&&c.fps==8);
 media_governor_build_transport_bitrate_range(&c,&r);assert(r.min_bitrate_bps<r.start_bitrate_bps&&r.start_bitrate_bps<r.max_bitrate_bps);
 c.bitrate_bps=1;c=media_governor_normalize_video_config(&c);assert(c.bitrate_bps==200000);
}
''')
if __name__=='__main__':unittest.main()
