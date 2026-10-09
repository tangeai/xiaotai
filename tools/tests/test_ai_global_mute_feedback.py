"""Replay public AI requests and assert actionable global-mute feedback."""
import unittest
from test_s3_product_regressions import ROOT, function, run_c

BK = ROOT / 'firmware/beken/bk7258/lckfb-bk7258-xiaotai'
RUNTIME = ROOT / 'platforms/esp-idf/components/starter_runtime/src/starter_runtime.c'

class AIGlobalMuteFeedback(unittest.TestCase):
    def test_ai_rejection_explains_global_mute_and_how_to_reenable(self):
        source = RUNTIME.read_text()
        start = function(source, 'begin_ai_session')
        # SDK/media acquisition follows this boundary; rejected starts must never reach it.
        start = start[:start.index('    starter_tirtc_accept_h5(false);')] + 'accepted++;}'
        run_c('#include \"' + str(ROOT / 'product/include/xiaotai_ai_feedback.h') + '\"\n' + r'''
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#define ESP_OK 0
#define EVENT_AI_START 1
#define ESP_LOGI(...) ((void)0)
#define ESP_LOGW(tag,...) snprintf(log_text,sizeof(log_text),__VA_ARGS__)
#define STARTER_RUNTIME_WAITING 0
#define STARTER_RUNTIME_H5_ACTIVE 1
#define STARTER_RUNTIME_AI_CONNECTING 2
#define STARTER_RUNTIME_AI_ACTIVE 3
#define STARTER_RUNTIME_CALL_ACTIVE 6
#define STARTER_RUNTIME_ROOM_CONNECTING 8
#define STARTER_RUNTIME_ROOM_ACTIVE 9
#define AI_READY_WAIT_TIMEOUT_MS 15000
typedef int starter_runtime_state_t;
typedef int esp_err_t;
static int state, queued, accepted, cancelled;
static bool ready=true, muted, s_room_page_active, s_ai_start_pending;
static int64_t s_ai_ready_deadline_ms;
static char message[193],log_text[512];
static int64_t now_ms(void){return 100;}
static int session_state(void){return state;}
static const char *starter_runtime_state_name(int s){return "test";}
static bool platform_client_ready(void){return ready;}
static bool starter_tirtc_started(void){return ready;}
static struct {bool microphone_muted;} starter_media_status(void){return (__typeof__(starter_media_status())){muted};}
static void starter_media_cancel_ai_preroll(uint32_t token){cancelled++;}
static void product_set_ai_start_pending(bool pending,const char *text){snprintf(message,sizeof(message),"%s",text);}
static void room_set_foreground(bool active){s_room_page_active=active;}
static int enqueue_simple(int event){queued=event;return ESP_OK;}
''' + function(source, 'starter_runtime_ai_start') + start + r'''
static void request(void){
 assert(starter_runtime_ai_start()==ESP_OK && queued==EVENT_AI_START);
 begin_ai_session(0);
}
int main(void){
 int states[]={STARTER_RUNTIME_WAITING,STARTER_RUNTIME_H5_ACTIVE,STARTER_RUNTIME_ROOM_ACTIVE};
 for(unsigned i=0;i<sizeof(states)/sizeof(states[0]);i++){
  state=states[i];muted=true;s_ai_start_pending=true;s_ai_ready_deadline_ms=123;
  for(int j=0;j<2;j++){
   request();assert(accepted==0 && muted);
   assert(!s_ai_start_pending && s_ai_ready_deadline_ms==0);
   assert(strcmp(message,"麦克风已关闭，请在设置中开启")==0);
   assert(strstr(log_text,"global microphone muted"));
  }
 }
 muted=false;state=STARTER_RUNTIME_CALL_ACTIVE;request();
 assert(strcmp(message,"现在暂时不能开始对话")==0 && accepted==0);
 state=STARTER_RUNTIME_WAITING;ready=false;request();
 assert(s_ai_start_pending && strstr(message,"AI 服务启动中"));
 ready=true;request();assert(accepted==1 && !muted);
 assert(cancelled==7);
}
''')

    def test_beken_public_start_keeps_global_mute_and_explains_recovery(self):
        source = (BK / 'ap/src/xiaotai_app.c').read_text()
        ui = (BK / 'ap/src/xiaotai_ui.c').read_text()
        handler = function(source, 'handle_intent')
        handler = handler[handler.index('    if (s_runtime.owner == XIAOTAI_OWNER_AI) {'):]
        feedback = ROOT / 'product/include/xiaotai_ai_feedback.h'
        includes = '#include "' + str(feedback) + '"\n' if feedback.exists() else ''
        includes += '#include "' + str(ROOT / 'product/include/xiaotai_ui_copy.h') + '"\n'
        run_c(r'''
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#define BK_OK 0
#define BK_ERR_NOT_INIT -1
#define BK_ERR_BUSY -2
#define BK_LOGI(...) ((void)0)
#define BK_LOGE(...) ((void)0)
#define BK_LOGW(tag,...) snprintf(log_text,sizeof(log_text),__VA_ARGS__)
#define XIAOTAI_OWNER_NONE 0
#define XIAOTAI_OWNER_AI 1
#define XIAOTAI_INTENT_PRIMARY 1
#define CONTROL_BUTTON 1
#define SESSION_CONNECT_TIMEOUT_MS 15000
static void *s_control_queue=(void *)1,*s_runtime_mutex=(void *)1;
typedef int xiaotai_session_owner_t;
typedef struct {int type,input_event;} control_event_t;
static struct {int owner;uint32_t generation;} s_runtime;
static struct {bool microphone_muted;} s_settings;
static uint32_t s_home_defer_until_ms;
static bool ready=true;
static int begins,requests,stops,locks;
static char log_text[256],shown[64];
static int rtos_lock_mutex(void *m){locks++;return 0;}
static int rtos_unlock_mutex(void *m){locks--;return 0;}
static uint32_t rtos_get_time(void){return 100;}
static bool xiaotai_runtime_has_incoming(void *r){return false;}
static void input_activity(void){}
static bool xiaotai_tirtc_ready(void){return ready;}
static bool xiaotai_tirtc_busy(void){return false;}
static bool xiaotai_runtime_begin(void *r,int owner,bool incoming){begins++;s_runtime.owner=owner;s_runtime.generation++;return true;}
static void stop_ai(uint32_t gen){stops++;}
static void arm_runtime_timeout_locked(uint32_t g,uint32_t ms){}
static void xiaotai_ui_show_status(const char *text){snprintf(shown,sizeof(shown),"%s",text);}
static void ai_token_response(void){}
static int xiaotai_platform_service_request(const char *path,void *body,void (*cb)(void),void *ctx){requests++;return 0;}
static void finish_ai(uint32_t gen,int rc){}
static void handle_start(void){
''' + includes + handler + r'''
static int rtos_push_to_queue(void *q,const control_event_t *event,int wait){
 assert(event->type==CONTROL_BUTTON && event->input_event==XIAOTAI_INTENT_PRIMARY);
 rtos_lock_mutex(&s_runtime_mutex);handle_start();return 0;
}
''' + function(source,'queue_console_intent') + function(source,'xiaotai_app_request_ai_start') + function(ui,'status_title') + function(ui,'status_hint') + r'''
int main(void){
 (void)input_activity;(void)rtos_get_time;(void)s_home_defer_until_ms;
 s_settings.microphone_muted=true;
 for(int i=0;i<2;i++){
  assert(xiaotai_app_request_ai_start()==BK_OK);
  assert(begins==0 && requests==0 && locks==0);
  assert(s_runtime.owner==XIAOTAI_OWNER_NONE && s_runtime.generation==0 && s_settings.microphone_muted);
  assert(strcmp(status_title(shown),"麦克风已关闭")==0);
  assert(strcmp(status_hint(shown),"请在设置中开启")==0);
  assert(strstr(log_text,"global microphone muted"));
  assert(s_home_defer_until_ms==5100);
 }
 ready=false;xiaotai_app_request_ai_start();assert(begins==0 && requests==0);
 s_settings.microphone_muted=false;ready=true;
 assert(xiaotai_app_request_ai_start()==BK_OK);
 assert(begins==1 && requests==1 && locks==0 && !s_settings.microphone_muted);
 s_settings.microphone_muted=true;rtos_lock_mutex(&s_runtime_mutex);handle_start();
 assert(stops==1 && locks==0); /* privacy must not prevent stopping an existing AI session */
}
''')

if __name__ == '__main__':
    unittest.main()
