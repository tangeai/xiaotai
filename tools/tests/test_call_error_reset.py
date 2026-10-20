"""Replay call admission: new calls clear historical errors, rejected calls do not."""
import unittest
from test_s3_product_regressions import ROOT, function, run_c
from test_camera_rotation_report import runtime_config


class CallErrorReset(unittest.TestCase):
    def test_outgoing_and_accepted_incoming_start_without_old_error(self):
        source = (ROOT / 'platforms/esp-idf/components/starter_runtime/src/starter_runtime.c').read_text()
        run_c(r'''
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdatomic.h>
#define STARTER_CONTACT_WECHAT 1
#define XIAOTAI_OWNER_NONE 0
#define XIAOTAI_OWNER_WECHAT_VOIP 1
#define XIAOTAI_OWNER_DEVICE_CALL 2
#define CALL_CONNECT_TIMEOUT_MS 30000U
#define CONFIG_IDF_TARGET_ESP32P4 1
#define PLATFORM_SERVICE_CALL 0
#define CALL_HTTP_DEVICE_INFO 0
#define ESP_OK 0
#define ESP_LOGW(...) ((void)0)
typedef int xiaotai_session_owner_t;
typedef int esp_err_t;
typedef struct {int source;char id[8],name[8],wx_app_id[8],wx_model_id[8];} starter_product_contact_t;
static atomic_int s_last_error;
static bool ready=true,started=true,preempt=true,admit=true,pending=true;
static bool s_voip_profile_ready=true,s_call_wechat,s_call_outgoing,s_call_video;
static int s_session,s_connection_generation,publications;
static char s_call_cleanup_room[8],s_call_peer_id[8],s_call_peer_name[8];
static char s_call_wx_app_id[8],s_call_wx_model_id[8],s_call_room_id[8];
static bool platform_client_ready(void){return ready;}
static bool starter_tirtc_started(void){return started;}
static bool preempt_for_call(bool incoming){(void)incoming;return preempt;}
static bool starter_tirtc_connected(void){return false;}
static int starter_tirtc_disconnect(void){return 0;}
static void starter_tirtc_accept_h5(bool b){(void)b;}
static void starter_media_stop(void){}
static void starter_media_set_call_video(bool b){(void)b;}
static bool session_incoming_pending(void){return pending;}
static bool xiaotai_runtime_begin(int*s,int owner,bool b){(void)s;(void)owner;(void)b;return admit;}
static bool xiaotai_runtime_accept_incoming(int*s,int*owner,uint32_t*gen){(void)s;*owner=2;*gen=7;return admit;}
static void product_set_call_result(const char*s){(void)s;}
static void product_snapshot_reset(void){}
static void product_set_call(bool a,bool b,const char*c,bool d){(void)a;(void)b;(void)c;(void)d;}
static void publish_state(void){assert(atomic_load(&s_last_error)==0);publications++;}
static void arm_session_timeout(unsigned t){(void)t;}
static void diagnostic_event(const char*s,bool b){(void)s;(void)b;}
static void connect_voip(void){}
static void call_http_response(void){}
static void*call_http_tag(uint32_t g,int stage){(void)g;(void)stage;return NULL;}
static int platform_client_request_timeout(int service,const char*path,const char*body,unsigned timeout,void(*cb)(void),void*tag){(void)service;(void)path;(void)body;(void)timeout;(void)cb;(void)tag;return 0;}
static void finish_call_session(int e,const char*s){(void)e;(void)s;}
''' + runtime_config(source) + function(source, 'begin_call_common') + function(source, 'accept_call') + r'''
int main(void){
 starter_product_contact_t c={0};
 for(int mode=0;mode<2;mode++){
  c.source=mode;
  for(int reject=0;reject<6;reject++){
   atomic_store(&s_last_error,-40007);publications=0;
   ready=reject!=0;started=reject!=1;preempt=reject!=2;admit=reject!=3;
   s_voip_profile_ready=reject!=4;s_call_cleanup_room[0]=reject==5?'x':0;
   bool expected=reject==4?mode==0:reject==5?mode==1:false;
   assert(begin_call_common(&c)==expected);
   assert(atomic_load(&s_last_error)==(expected?0:-40007));
  }
  ready=started=preempt=admit=s_voip_profile_ready=true;s_call_cleanup_room[0]=0;
  atomic_store(&s_last_error,-40007);assert(begin_call_common(&c));
  assert(atomic_load(&s_last_error)==0);
  s_call_wechat=mode;
  for(int reject=0;reject<4;reject++){
   pending=reject!=0;preempt=reject!=1;admit=reject!=2;
   atomic_store(&s_last_error,-40007);publications=0;
   accept_call();
   assert(atomic_load(&s_last_error)==(reject==3?0:-40007));
   assert(publications==(reject==3?1:0));
  }
 }
}
''')


if __name__ == '__main__':
    unittest.main()
