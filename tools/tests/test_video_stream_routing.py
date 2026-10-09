"""Replay actual ESP video send/subscribe/keyframe callbacks against media contracts."""
import re
import unittest
from test_s3_product_regressions import ROOT, function, run_c
from test_s3_voip_audio import COMMON

SOURCE=ROOT/'platforms/esp-idf/components/starter_tirtc/src/starter_tirtc.c'

class VideoStreams(unittest.TestCase):
    def test_video_ids_and_wrong_stream_unsubscribe(self):
        source=SOURCE.read_text()
        constants='\n'.join(re.findall(r'^#define (?:H5_VIDEO_STREAM|H5_DOWN_VIDEO_STREAM|VOIP_VIDEO_STREAM) .*$',source,re.M))
        helpers='\n'.join(function(source,name) for name in ('video_stream_for_mode','down_video_stream_for_mode') if re.search(r'static uint8_t '+name+r'\(',source))
        funcs='\n'.join(function(source,name) for name in ('on_request_key_frame','on_subscribe_video','on_unsubscribe_video','starter_tirtc_send_h264','starter_tirtc_subscribe_call_video','starter_tirtc_request_remote_key_frame'))
        contract=(ROOT/'product/src/xiaotai_media_contract.c').read_text().replace('#include "xiaotai_media_contract.h"','')
        run_c(COMMON+r'''
#undef CONFIG_IDF_TARGET_ESP32P4
#define CONFIG_IDF_TARGET_ESP32P4 1
#define TIRTC_VIDEO_H264 9
#define TIRTC_FRAME_FLAG_KEY_FRAME 1
static atomic_bool s_video_subscribed;
static atomic_uint s_active_generation=7;
static unsigned keyframes,commands,down_expected;
static void keyframe(uint32_t gen,void *ctx){assert(gen==7);keyframes++;}
static struct {void (*on_key_frame)(uint32_t,void *);void *user_data;} s_handlers={keyframe,0};
static uint32_t starter_tirtc_generation(void){return 7;}
static bool starter_tirtc_video_ready(void){return true;}
static int TiRtcSendVideoStream(void *c,TIRTCFRAMEINFO *f,const void *data){
 assert(c==(void *)1 && data && f->stream_id==expected);return 0;
}
static int TiRtcSubscribeVideo(void *c,unsigned stream){
 assert(c==(void *)1 && stream==down_expected);subscriptions++;return subscribe_result;
}
static int TiRtcRequestKeyFrame(void *c,unsigned stream){
 assert(c==(void *)1 && stream==down_expected);return 0;
}
static int TiRtcSendCommand(void *c,unsigned cmd,const void *data,unsigned len){commands++;return 0;}
'''+constants+'\n'+helpers+'\n'+funcs+'\n#include "'+str(ROOT/'product/include/xiaotai_media_contract.h')+'"\n'+contract+r'''
int main(void){
 int modes[]={STARTER_TIRTC_VOIP,STARTER_TIRTC_CALL,STARTER_TIRTC_H5};
 xiaotai_media_mode_t contracts[]={XIAOTAI_MEDIA_MODE_WECHAT_VOIP,XIAOTAI_MEDIA_MODE_DEVICE_CALL,XIAOTAI_MEDIA_MODE_STREAM};
 for(unsigned round=0;round<100;round++)for(unsigned i=0;i<3;i++){
  xiaotai_media_contract_t c;assert(xiaotai_media_contract_get(contracts[i],&c));
  current_mode=modes[i];expected=c.up_video_stream_id;down_expected=c.down_video_stream_id;
  assert(starter_tirtc_send_h264(20,"x",10,true)==0);
  unsigned k=keyframes;
  assert(on_subscribe_video((void *)1,expected)==0 && s_video_subscribed && keyframes==k+1);
  assert(on_subscribe_video((void *)2,expected)<0);
  on_request_key_frame((void *)1,expected+2);assert(keyframes==k+1);
  on_unsubscribe_video((void *)1,expected+2);assert(s_video_subscribed);
  on_unsubscribe_video((void *)2,expected);assert(s_video_subscribed);
  on_unsubscribe_video((void *)1,expected);assert(!s_video_subscribed);
  if(current_mode!=STARTER_TIRTC_H5)assert(starter_tirtc_subscribe_call_video()==0);
  assert(starter_tirtc_request_remote_key_frame()==0);
 }
 s_connection=0;assert(starter_tirtc_request_remote_key_frame()<0);
}
''')

if __name__=='__main__':unittest.main()
