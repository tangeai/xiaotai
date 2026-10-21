"""S3 audio-only calls through runtime, UI and SDK seams."""
import re
import unittest
from test_s3_product_regressions import ROOT, function, run_c
from test_camera_rotation_report import FAKES, requests, runtime_config, selected_defines
from test_s3_voip_audio import COMMON

BASE = ROOT / 'platforms/esp-idf/components'
PROJECT = ROOT / 'firmware/esp-idf/esp32s3/m5stack-cores3-lite/components'


def defines(component, board):
    if board != "m5stack":
        return ""
    cmake = (PROJECT / component / 'CMakeLists.txt').read_text()
    return ''.join(f'#define {k} {v}\n' for k, v in re.findall(
        r'(XIAOTAI_BOARD_\w+)=("[^"\n]+"|\d+)', cmake))


class CoreS3AudioCalls(unittest.TestCase):
    board = "m5stack"
    def test_advertisement_preserves_h5_and_disables_both_voip_video_directions(self):
        profiles = requests(self.board)['/v1/device/profile']['profiles']
        self.assertTrue(profiles['voip']['no_video'])
        self.assertEqual(profiles['voip']['up_video_mt'], 'none')
        self.assertEqual(profiles['voip']['down_video_mt'], 'none')
        self.assertEqual(profiles['voip']['audio_rate'], 8000)
        self.assertEqual(profiles['voip']['down_audio_mt'], 'alaw')
        self.assertEqual(profiles['stream']['up_video_mt'], ['mjpeg'])
        if self.board == 'm5stack':
            self.assertEqual(profiles['stream']['camera_rotation'], 180)
        self.assertFalse(profiles['stream']['no_video'])
        self.assertTrue(profiles['call']['no_video'])

    def test_runtime_clamps_explicit_video_and_stale_pending_state_to_voice(self):
        source = (BASE / 'starter_runtime/src/starter_runtime.c').read_text()
        fakes = FAKES.replace('static void starter_media_set_call_video(bool v){}',
            'static bool media_video; static void starter_media_set_call_video(bool v){media_video=v;}')
        fakes = fakes.replace('printf("%s\\t%s\\n",url,body);', r'''
            if(service==PLATFORM_SERVICE_VOIP) {
                assert(strstr(body,"\"wx_room_type\":\"voice\""));
                assert(!strstr(body,"\"video\""));
            }''')
        run_c(selected_defines(self.board) + runtime_config(source) + fakes + '\n'.join(
            function(source, n) for n in ('call_video_for_request', 'dial_contact', 'accept_call')) + r'''
int main(void){
    (void)s_voip_profile_retry_at_ms;(void)s_voip_profile_inflight;(void)s_call_camera_enabled;
    const char *requests[]={NULL,"","video","voice","audio"};
    for(unsigned i=0;i<5;i++) {
        assert(!call_video_for_request(requests[i],true));
        assert(!call_video_for_request(requests[i],false));
    }
    s_call_wechat=true;
    dial_contact(0,true);assert(!s_call_video && !media_video);
    dial_contact(0,false);assert(!s_call_video && !media_video);
    s_call_video=true;media_video=true;accept_call();
    assert(!s_call_video && !media_video);
    s_call_wechat=false;dial_contact(0,true);assert(!s_call_video && !media_video);
}
''')

    def test_media_policy_cannot_enable_voip_camera(self):
        source = (BASE / 'starter_media_s3/starter_media.c').read_text()
        run_c(defines('starter_media', self.board) + r'''
#include <assert.h>
#include <stdbool.h>
#include <stdatomic.h>
#define STARTER_TIRTC_H5 1
#define STARTER_TIRTC_AI 2
#define STARTER_TIRTC_VOIP 3
#define STARTER_TIRTC_CALL 4
#define STARTER_TIRTC_ROOM 5
typedef int starter_tirtc_mode_t;
static atomic_bool s_call_video;
''' + function(source,'starter_media_set_call_video') + function(source,'video_enabled_for_mode') + r'''
int main(void){
    for(int video=0;video<=1;video++) {
        starter_media_set_call_video(video);
        assert(video_enabled_for_mode(STARTER_TIRTC_H5));
        for(int mode=2;mode<=5;mode++) assert(!video_enabled_for_mode(mode));
    }
}
''')

    def test_sdk_rejects_voip_jpeg_even_if_subscription_is_stale(self):
        source = (BASE / 'starter_tirtc/src/starter_tirtc.c').read_text()
        constants='\n'.join(re.findall(r'^#define (?:H5_VIDEO_STREAM|VOIP_VIDEO_STREAM) .*$',source,re.M))
        run_c(defines('starter_tirtc', self.board) + COMMON + r'''
#define TIRTC_E_BUSY -9
#define TIRTC_VIDEO_JPEG 8
#define TIRTC_FRAME_FLAG_KEY_FRAME 1
static atomic_bool s_video_subscribed;
static atomic_uint s_send_buffer_limit=262144;
static bool connected=true;
static unsigned sends;
static bool starter_tirtc_connected(void){return connected;}
static size_t TiRtcGetSendBufferUsed(void*c){return 0;}
static int TiRtcSendVideoStream(void*c,TIRTCFRAMEINFO*f,const void*d){
 assert(current_mode==STARTER_TIRTC_H5 && f->stream_id==11);sends++;return 0;
}
''' + constants + '\n' + '\n'.join(function(source,n) for n in
            ('video_stream_for_mode','starter_tirtc_video_ready','starter_tirtc_send_mjpeg')) + r'''
int main(void){
 current_mode=STARTER_TIRTC_VOIP;s_video_subscribed=true;
 assert(!starter_tirtc_video_ready());
 assert(starter_tirtc_send_mjpeg(42,"jpeg",100)<0 && sends==0);
 current_mode=STARTER_TIRTC_CALL;
 assert(!starter_tirtc_video_ready());
 assert(starter_tirtc_send_mjpeg(42,"jpeg",100)<0 && sends==0);
 current_mode=STARTER_TIRTC_H5;
 assert(starter_tirtc_video_ready());
 assert(starter_tirtc_send_mjpeg(42,"jpeg",100)==0 && sends==1);
}
''')

    def test_contact_ui_has_only_audio_for_either_contact_source(self):
        source=(BASE / 'starter_product/src/starter_product.c').read_text()
        run_c(defines('starter_product', self.board) + r'''
#include <assert.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#define CONFIG_IDF_TARGET_ESP32P4 0
#define STARTER_CONTACT_WECHAT 1
#define LV_TEXT_ALIGN_CENTER 0
#define ACTION_CONTACTS_BACK 1
#define ACTION_VOICE_CALL 2
#define ACTION_VIDEO_CALL 3
typedef int lv_obj_t;
typedef struct {int source;char name[32];} starter_product_contact_t;
typedef struct {unsigned contact_count;starter_product_contact_t contacts[1];} starter_runtime_product_snapshot_t;
static starter_runtime_product_snapshot_t snapshot={.contact_count=1};
static unsigned s_selected_contact,voice;
static starter_runtime_product_snapshot_t starter_runtime_product_snapshot(void){return snapshot;}
static void make_header_back_button(lv_obj_t*s,int a){}
static void render_header(lv_obj_t*s,const char*t){}
static int lv_color_hex(unsigned c){return c;}
static lv_obj_t *make_label(lv_obj_t*s,const char*t,int x,int y,int w,int c){return s;}
static void lv_obj_set_style_text_align(lv_obj_t*s,int a,int b){}
static void make_button(lv_obj_t*s,const char*t,int x,int y,int w,int h,int a){
 assert(a==ACTION_VOICE_CALL);assert(x>=0 && x+w<=320 && y+h<=240);voice++;
}
''' + function(source,'render_contact_detail') + r'''
int main(void){
 snapshot.contacts[0].source=STARTER_CONTACT_WECHAT;render_contact_detail(NULL);
 snapshot.contacts[0].source=0;render_contact_detail(NULL);assert(voice==2);
 s_selected_contact=1;render_contact_detail(NULL);assert(voice==2);
}
''')


class LckfbAudioCalls(CoreS3AudioCalls):
    board = 'lckfb'


if __name__ == '__main__':
    unittest.main()
