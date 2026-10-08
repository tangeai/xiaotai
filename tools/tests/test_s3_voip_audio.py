"""Replay ESP32 audio routing against the shared contract and BK VoIP IDs."""
import re
import unittest
from test_s3_product_regressions import ROOT, function, run_c
TIRTC=ROOT/'platforms/esp-idf/components/starter_tirtc/src/starter_tirtc.c'
COMMON=r'''
#include <assert.h>
#include <stdatomic.h>
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#define ESP_LOGI(...) ((void)0)
#define CONFIG_IDF_TARGET_ESP32P4 0
#define TIRTC_E_INVALID_PARAMETER -1
#define TIRTC_AUDIO_ALAW 1
#define TIRTC_AUDIOSAMPLE_8K16B1C 0
#define STARTER_TIRTC_H5 1
#define STARTER_TIRTC_AI 2
#define STARTER_TIRTC_VOIP 3
#define STARTER_TIRTC_CALL 4
#define STARTER_TIRTC_ROOM 5
typedef int starter_tirtc_mode_t;
typedef void *tirtc_conn_t;
typedef struct {unsigned stream_id,media,flags,ts,length;} TIRTCFRAMEINFO;
atomic_uintptr_t s_connection=1;
atomic_bool s_audio_subscribed;
static atomic_int s_mode;
#define current_mode s_mode
static unsigned expected,subscriptions;
static int subscribe_result;
static int starter_tirtc_mode(void){return current_mode;}
static bool connection_matches(void *c){return c==(void *)1;}
static int TiRtcSendAudioStream(void *c,TIRTCFRAMEINFO *f,const void *data){
 assert(c==(void *)1 && data && f->stream_id==expected && f->ts==20 && f->length==160);
 return 0;
}
static int TiRtcSubscribeAudio(void *c,unsigned stream){
 assert(c==(void *)1 && stream==expected);subscriptions++;return subscribe_result;
}
'''

class S3VoipAudio(unittest.TestCase):
    def code(self):
        source=TIRTC.read_text()
        constants='\n'.join(re.findall(r'^#define (?:H5_AUDIO_STREAM|CALL_AUDIO_STREAM|AI_AUDIO_STREAM|VOIP_AUDIO_STREAM|ROOM_AUDIO_STREAM) .*$',source,re.M))
        helper=function(source,'audio_stream_for_mode') if 'static uint8_t audio_stream_for_mode(' in source else ''
        return COMMON+constants+'\n'+helper

    def test_uplink_and_subscription_ids_follow_public_contract(self):
        contract=(ROOT/'product/src/xiaotai_media_contract.c').read_text().replace('#include "xiaotai_media_contract.h"','')
        run_c(self.code()+'\n#include "'+str(ROOT/'product/include/xiaotai_media_contract.h')+'"\n'+contract+
              function(TIRTC.read_text(),'starter_tirtc_send_alaw')+
              function(TIRTC.read_text(),'on_subscribe_audio')+
              function(TIRTC.read_text(),'on_unsubscribe_audio')+r'''
int main(void){
 int modes[]={3,5,1,4};
 xiaotai_media_mode_t contracts[]={XIAOTAI_MEDIA_MODE_WECHAT_VOIP,XIAOTAI_MEDIA_MODE_ROOM,
 XIAOTAI_MEDIA_MODE_STREAM,XIAOTAI_MEDIA_MODE_DEVICE_CALL};
 for(unsigned round=0;round<100;round++)for(unsigned i=0;i<4;i++){
  xiaotai_media_contract_t c;assert(xiaotai_media_contract_get(contracts[i],&c));
  current_mode=modes[i];expected=c.up_audio_stream_id;
  assert(starter_tirtc_send_alaw(20,"x",160)==0);
  s_audio_subscribed=false;assert(on_subscribe_audio((void *)1,expected)==0);
  assert(s_audio_subscribed && on_subscribe_audio((void *)1,expected+2)==-1);
  on_unsubscribe_audio((void *)1,expected+2);assert(s_audio_subscribed);
  on_unsubscribe_audio((void *)1,expected);assert(!s_audio_subscribed);
  assert(on_subscribe_audio((void *)2,expected)==-1);
 }
 s_connection=0;assert(starter_tirtc_send_alaw(20,"x",160)==-1);
}
''')

    def test_explicit_call_downlink_subscription_and_failure(self):
        run_c(self.code()+function(TIRTC.read_text(),'starter_tirtc_subscribe_call_audio')+r'''
int main(void){
 current_mode=STARTER_TIRTC_VOIP;expected=0;
 assert(starter_tirtc_subscribe_call_audio()==0 && subscriptions==1);
 subscribe_result=-42;assert(starter_tirtc_subscribe_call_audio()==-42);
 current_mode=STARTER_TIRTC_CALL;expected=10;subscribe_result=0;
 assert(starter_tirtc_subscribe_call_audio()==0 && subscriptions==3);
 current_mode=STARTER_TIRTC_AI;assert(starter_tirtc_subscribe_call_audio()==-1);
 s_connection=0;current_mode=STARTER_TIRTC_VOIP;
 assert(starter_tirtc_subscribe_call_audio()==-1 && subscriptions==3);
}
''')

    def test_voip_media_waits_for_confirmation_and_subscription(self):
        runtime=(ROOT/'platforms/esp-idf/components/starter_runtime/src/starter_runtime.c').read_text()
        run_c(COMMON+r'''
#include <string.h>
#define ESP_OK 0
#define ESP_ERR_INVALID_STATE -2
#define STARTER_RUNTIME_CALL_CONNECTING 1
#define CALL_COMMAND_CONNECT 0x2000
#define CALL_COMMAND_HANGUP 0x2001
typedef int starter_runtime_state_t;
typedef struct {int mode;uint32_t generation,command;unsigned length;char text[64];} runtime_event_t;
typedef struct {char *valuestring;} cJSON;
static bool s_call_wechat=true,s_call_waiting_confirm=true,s_call_outgoing;
static bool s_call_peer_answered;
static uint32_t s_connection_generation=7;
static char s_call_room_id[16];
static int s_session,state=1,started,finished,published;
static int session_state(void){return state;}
static uint32_t session_generation(void){return s_connection_generation;}
static void finish_call_session(int error,const char *msg){finished++;state=0;s_call_waiting_confirm=false;}
static void finish_session(int error){finished++;}
static cJSON *cJSON_ParseWithLength(const char *s,unsigned n){return NULL;}
static cJSON *cJSON_GetObjectItemCaseSensitive(cJSON *r,const char *k){return NULL;}
static bool cJSON_IsString(const cJSON *r){return false;}
static void cJSON_Delete(cJSON *r){}
static void maybe_activate_outgoing_device_call(const char *source){}
static int starter_tirtc_subscribe_call_audio(void){subscriptions++;return subscribe_result;}
static int starter_media_start(int mode,uint32_t gen){assert(subscriptions>0 && mode==3 && gen==s_connection_generation);started++;return 0;}
static bool xiaotai_runtime_media_started(int *s,uint32_t gen){state=2;return true;}
static void publish_state(void){published++;}
'''+function(runtime,'handle_call_command')+r'''
int main(void){
 runtime_event_t e={.mode=3,.generation=6,.command=0x2000};
 handle_call_command(&e);assert(!started && !subscriptions);
 e.generation=7;e.command=0x999;handle_call_command(&e);assert(!started);
 e.command=0x2000;subscribe_result=-42;handle_call_command(&e);
 assert(subscriptions==1 && !started && finished==1 && !published);
 handle_call_command(&e);assert(subscriptions==1 && !started);
 state=1;s_call_waiting_confirm=true;s_connection_generation=8;
 handle_call_command(&e);assert(subscriptions==1 && !started);
 e.generation=8;
 subscribe_result=0;handle_call_command(&e);
 assert(subscriptions==2 && started==1 && published==1 && !s_call_waiting_confirm);
 handle_call_command(&e);assert(started==1 && subscriptions==2);
}
''')

if __name__=='__main__':unittest.main()
