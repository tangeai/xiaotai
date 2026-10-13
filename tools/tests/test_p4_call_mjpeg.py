"""Exercise the CALL-only MJPEG budget and media routing contract."""
import unittest

from test_s3_product_regressions import ROOT, function, run_c

P4 = ROOT / "firmware/esp-idf/esp32p4/waveshare-touch-lcd-43c"


class CallMjpeg(unittest.TestCase):
    def test_quality_budget_and_codec_isolation(self):
        source = (P4 / "main/media/camera_pipeline.c").read_text()
        run_c(r'''
#include <assert.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "''' + str(P4 / "main/media/media_tuning.h") + r'''"
''' + function(source, "camera_pipeline_mjpeg_budget_accept") + r'''
int main(void) {
    uint64_t credit=0, last=0;
    assert(APP_MEDIA_CALL_JPEG_QUALITY==75);
    assert(APP_MEDIA_CALL_VIDEO_WIDTH==640&&APP_MEDIA_CALL_VIDEO_HEIGHT==480);
    assert(APP_MEDIA_CALL_VIDEO_FPS==8&&APP_MEDIA_CALL_VIDEO_BITRATE_BPS==4000000);
    assert(APP_MEDIA_WECHAT_VIDEO_WIDTH==320&&APP_MEDIA_WECHAT_VIDEO_HEIGHT==240);
    assert(APP_MEDIA_WECHAT_VIDEO_FPS==15&&APP_MEDIA_WECHAT_VIDEO_BITRATE_BPS==200000);
    assert(camera_pipeline_mjpeg_budget_accept(&credit,&last,1000000,100000));
    assert(!camera_pipeline_mjpeg_budget_accept(&credit,&last,1000000,450000));
    assert(camera_pipeline_mjpeg_budget_accept(&credit,&last,2000000,400000));
}
''')
        worker = function(source, "camera_pipeline_task")
        self.assertIn("camera_pipeline_run_call_mjpeg();", worker)
        self.assertIn("starter_tirtc_mode() == STARTER_TIRTC_CALL", worker)
        self.assertIn("TIRTC_VIDEO_JPEG, true", source)
        runtime = (ROOT / "platforms/esp-idf/components/starter_runtime/src/starter_runtime.c").read_text()
        call = runtime[runtime.index('"\\\"call\\\":'):runtime.index('"\\\"voip\\\":')]
        voip = runtime[runtime.index('"\\\"voip\\\":'):runtime.index("#else", runtime.index('"\\\"voip\\\":'))]
        self.assertIn('up_video_mt\\\":[\\\"mjpeg', call)
        self.assertIn('down_video_mt\\\":[\\\"mjpeg', call)
        self.assertIn('up_video_mt\\\":\\\"h264', voip)
        self.assertIn('down_video_mt\\\":\\\"mjpeg', voip)

    def test_device_call_poll_starts_mjpeg_renderer(self):
        source = (P4 / "components/p4_hardware/p4_video.c").read_text()
        poll = function(source, "p4_video_poll")
        route = function(source, "p4_video_downlink_codec_for_mode")
        run_c(r'''
#include <assert.h>
typedef int starter_tirtc_mode_t;
typedef int call_video_codec_t;
#define STARTER_TIRTC_H5 1
#define STARTER_TIRTC_CALL 2
#define STARTER_TIRTC_VOIP 3
#define CALL_VIDEO_CODEC_H264 1
#define CALL_VIDEO_CODEC_MJPEG 2
''' + route + r'''
int main(void) {
    assert(p4_video_downlink_codec_for_mode(STARTER_TIRTC_CALL)==CALL_VIDEO_CODEC_MJPEG);
    assert(p4_video_downlink_codec_for_mode(STARTER_TIRTC_VOIP)==CALL_VIDEO_CODEC_MJPEG);
    assert(p4_video_downlink_codec_for_mode(STARTER_TIRTC_H5)==CALL_VIDEO_CODEC_H264);
}
''')
        self.assertIn("p4_video_downlink_codec_for_mode(mode)", poll)
        app = (P4 / "main/application/app.c").read_text()
        acquire = function(app, "app_acquire_call_session_resources")
        self.assertIn("call_video_renderer_start_for_codec(CALL_VIDEO_CODEC_MJPEG)", acquire)


if __name__ == "__main__":
    unittest.main()
