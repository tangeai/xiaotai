#!/usr/bin/env python3
"""Actual public camera policy and scaler geometry preserve call field of view."""
from pathlib import Path
import subprocess
import tempfile
root=Path(__file__).resolve().parents[1]
governor=(root/"main/media/media_governor.c").read_text()
scaler=(Path(__file__).resolve().parents[5]/"platforms/esp-idf/waveshare_p4/video_yuv420_scaler.c").read_text()
def function(text, signature):
    start=text.index(signature)
    return text[start:text.index("\n}",start)+2]
code=r'''
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>
#include <stddef.h>
#include "media_tuning.h"
#include "media_governor.h"
#define CONFIG_APP_RTC_VIDEO_AUTO_ADAPT_ENABLE 0
#define MEDIA_GOVERNOR_CAPTURE_WIDTH APP_MEDIA_CAMERA_CAPTURE_WIDTH
#define MEDIA_GOVERNOR_CAPTURE_HEIGHT APP_MEDIA_CAMERA_CAPTURE_HEIGHT
#define MEDIA_GOVERNOR_FULL_WIDTH APP_MEDIA_RTC_VIDEO_WIDTH
#define MEDIA_GOVERNOR_FULL_HEIGHT APP_MEDIA_RTC_VIDEO_HEIGHT
#define MEDIA_GOVERNOR_FULL_FPS APP_MEDIA_RTC_H264_FPS
#define MEDIA_GOVERNOR_VIDEO_MIN_BITRATE_BPS 200000U
#define taskENTER_CRITICAL(lock) ((void)(lock))
#define taskEXIT_CRITICAL(lock) ((void)(lock))
#define ESP_RETURN_ON_FALSE(ok, error, ...) do { if (!(ok)) return error; } while (0)
#define ESP_LOGI(...) ((void)old_config)
#define VIDEO_YUV420_SCALE_DENOMINATOR 16U
static media_governor_video_config_t s_rtc_video_config;
static media_governor_video_config_t s_adaptation_base_config;
static int s_lock;
static bool s_initialized, s_transport_adaptation_active;
static uint32_t s_transport_requested_bitrate_bps;
static uint32_t s_transport_protection_not_before_tick, s_transport_recovery_not_before_tick;
typedef struct {uint16_t input_width,input_height,output_width,output_height; bool rotate_ccw90,fit_contain;} video_yuv420_scaler_config_t;
'''
code+=function(governor,"void media_governor_build_device_call_video_config(")
code+=function(governor,"void media_governor_build_wechat_video_config(")
code+=function(governor,"void media_governor_build_h5_video_config(")
code+=function(governor,"static media_governor_video_config_t media_governor_normalize_video_config(")
code+=function(governor,"esp_err_t media_governor_set_rtc_video_config(")
code+=function(governor,"static void media_governor_select_native_capture_size(")
code+=function(governor,"static media_governor_camera_policy_t media_governor_make_rtc_av_policy(")
code+=function(governor,"void media_governor_build_camera_policy(")
code+=function(scaler,"static bool video_yuv420_select_geometry(")
code+=function(scaler,"static bool video_yuv420_config_valid(")
code+=function(scaler,"static size_t video_yuv420_data_size(")
code+=function(scaler,"static void video_yuv420_fill_black_ouyy_evyy(")
code+=r'''
int main(void) {
    uint8_t black[24];
    memset(black, 0xa5, sizeof(black));
    video_yuv420_fill_black_ouyy_evyy(black, 4, 4);
    const uint8_t expected_black[24] = {
        128, 16, 16, 128, 16, 16,
        128, 16, 16, 128, 16, 16,
        128, 16, 16, 128, 16, 16,
        128, 16, 16, 128, 16, 16,
    };
    assert(memcmp(black, expected_black, sizeof(black)) == 0);
    media_governor_video_config_t config;
    media_governor_build_device_call_video_config(&config);
    assert(config.width==640 && config.height==480 && config.fps==5 && config.bitrate_bps==150000);
    uint16_t w=APP_MEDIA_CAMERA_CAPTURE_WIDTH,h=APP_MEDIA_CAMERA_CAPTURE_HEIGHT;
    media_governor_camera_policy_t policy;
    media_governor_build_camera_policy(&config,&policy);
    w=policy.capture_width; h=policy.capture_height;
    assert(w==640 && h==480);
    assert(policy.capture_fps==5 && policy.rtc_video_fps==5);
    assert(policy.rtc_width==640 && policy.rtc_height==480);
    assert(policy.h264_bitrate_bps==150000);
    video_yuv420_scaler_config_t scale={w,h,config.width,config.height,false,true};
    assert(video_yuv420_config_valid(&scale));
    uint16_t cw,ch,x,y,ox,oy; uint8_t step;
    assert(video_yuv420_select_geometry(&scale,&cw,&ch,&x,&y,&ox,&oy,&step));
    assert(cw==640 && ch==480 && x==0 && y==0 && ox==0 && oy==0 && step==16);
    media_governor_build_wechat_video_config(&config);
    assert(config.width==640 && config.height==480 && config.fps==8 && config.bitrate_bps==500000);
    media_governor_build_camera_policy(&config,&policy);
    assert(policy.rtc_width==640 && policy.rtc_height==480);
    assert(policy.capture_fps==8 && policy.rtc_video_fps==8 && policy.h264_bitrate_bps==500000);
    w=APP_MEDIA_CAMERA_CAPTURE_WIDTH; h=APP_MEDIA_CAMERA_CAPTURE_HEIGHT;
    media_governor_select_native_capture_size(&config,&w,&h);
    assert(w==640 && h==480);
    scale=(video_yuv420_scaler_config_t){w,h,config.width,config.height,false,false};
    assert(video_yuv420_config_valid(&scale));
    assert(video_yuv420_select_geometry(&scale,&cw,&ch,&x,&y,&ox,&oy,&step));
    assert(cw==640 && ch==480 && x==0 && y==0 && step==16);
    media_governor_build_h5_video_config(&config);
    assert(config.width==1280 && config.height==960 && config.fps==20 && config.bitrate_bps==3000000);
    w=APP_MEDIA_CAMERA_CAPTURE_WIDTH; h=APP_MEDIA_CAMERA_CAPTURE_HEIGHT;
    media_governor_select_native_capture_size(&config,&w,&h);
    assert(w==1280 && h==960);
    scale=(video_yuv420_scaler_config_t){w,h,config.width,config.height,false,false};
    assert(video_yuv420_config_valid(&scale));
    assert(video_yuv420_select_geometry(&scale,&cw,&ch,&x,&y,&ox,&oy,&step));
    /* Repeated scene changes discard the previous scene's degraded target. */
    void (*builders[])(media_governor_video_config_t *) = {
        media_governor_build_wechat_video_config,
        media_governor_build_h5_video_config,
        media_governor_build_device_call_video_config,
    };
    const uint16_t widths[]={640,1280,640}, heights[]={480,960,480};
    const uint8_t fps[]={8,20,5};
    const uint32_t rates[]={500000,3000000,150000};
    assert(media_governor_set_rtc_video_config(NULL)==ESP_ERR_INVALID_ARG);
    for (unsigned round=0; round<100; ++round) {
        for (unsigned mode=0; mode<3; ++mode) {
            builders[mode](NULL);
            builders[mode](&config);
            assert(media_governor_set_rtc_video_config(&config)==ESP_OK);
            assert(s_rtc_video_config.width==widths[mode] && s_rtc_video_config.height==heights[mode]);
            assert(s_rtc_video_config.fps==fps[mode] && s_rtc_video_config.bitrate_bps==rates[mode]);
            assert(s_adaptation_base_config.bitrate_bps==rates[mode]);
            assert(!s_transport_adaptation_active && s_transport_requested_bitrate_bps==0);
            config.bitrate_bps=200000;
            config.weak_network_mode=MEDIA_GOVERNOR_WEAK_NETWORK_FRAMERATE_PRIORITY;
            config.weak_network_level=3;
            assert(media_governor_set_rtc_video_config(&config)==ESP_OK);
            assert(s_adaptation_base_config.bitrate_bps==rates[mode]);
            s_transport_adaptation_active=true;
            s_transport_requested_bitrate_bps=200000;
        }
    }
    assert(cw==1280 && ch==960 && x==0 && y==0 && step==16);
    scale=(video_yuv420_scaler_config_t){800,640,384,256,false,false};
    assert(video_yuv420_config_valid(&scale));
    assert(video_yuv420_select_geometry(&scale,&cw,&ch,&x,&y,&ox,&oy,&step));
}
'''
with tempfile.TemporaryDirectory(prefix="full-frame-uplink-") as tmp:
    p=Path(tmp); (p/"test.c").write_text(code)
    (p/"esp_err.h").write_text("typedef int esp_err_t;\n#define ESP_OK 0\n#define ESP_ERR_INVALID_ARG 258\n")
    subprocess.run(["cc","-Wall","-Wextra","-Werror","-I",str(p),"-I",str(root/"main/media"),str(p/"test.c"),"-o",str(p/"test")],check=True)
    subprocess.run([str(p/"test")],check=True)
video=(root/"components/p4_hardware/p4_video.c").read_text()
assert "mode == STARTER_TIRTC_H5" in video
assert "media_governor_build_h5_video_config(&config);" in video
for config in ("sdkconfig.defaults", "sdkconfig"):
    config_path = root / config
    if config_path.exists():
        assert "CONFIG_APP_RTC_H264_RESOURCE_FALLBACK_ENABLE=y" not in config_path.read_text()
pipeline=(root/"main/media/camera_pipeline.c").read_text()
assert "const bool rotate_ccw90 = false;" in pipeline
assert '"yuv420-ppa-ccw90"' not in pipeline
print("PASS: public call camera policy uses native VGA for calls and full capture for H5")
