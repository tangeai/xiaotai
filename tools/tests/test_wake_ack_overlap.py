"""Exercise acoustic admission via real board media and shared PCM queue."""
import unittest
from test_s3_product_regressions import ROOT, function, run_c

COMMON = ROOT / 'platforms/esp-idf/components/starter_media_common'
MEDIA = [ROOT / f'firmware/esp-idf/{path}/components/starter_media/src/starter_media.c' for path in (
    'esp32s3/lckfb-esp32s3-xiaotai', 'esp32p4/waveshare-touch-lcd-43c')]

PREFIX = r'''
#include <assert.h>
#include <stdatomic.h>
#include "starter_preroll.h"
#define pdTRUE 1
#define pdMS_TO_TICKS(x) (x)
#define portMAX_DELAY 0
static int s_preroll_mutex_storage;
static void *s_preroll_mutex=&s_preroll_mutex_storage;
static atomic_bool s_microphone_muted, s_active, s_wake_allowed=true;
static starter_preroll_t s_preroll;
static int64_t now=1000;
static int xSemaphoreTake(void *p,int wait){(void)p;(void)wait;return 1;}
static void xSemaphoreGive(void *p){(void)p;}
static int64_t esp_timer_get_time(void){return now*1000;}
static int16_t memory[160000], frame[160];
static void append(int marker,int from,int to){
 for(int i=0;i<160;i++)frame[i]=marker;
 for(int ms=from;ms<=to;ms+=20)starter_preroll_append(&s_preroll,frame,160,ms);
}
'''

class WakeAckOverlap(unittest.TestCase):
    def test_fast_ai_cannot_bind_during_local_ack(self):
        for path in MEDIA:
            with self.subTest(board=path.parts[-5]):
                run_c(PREFIX + (COMMON/'src/starter_preroll.c').read_text() +
                      function(path.read_text(),'starter_media_prepare_ai_preroll') + r'''
int main(void){
 starter_preroll_init(&s_preroll,memory,160000);
 append(11,20,1000);
 unsigned token=starter_media_prepare_ai_preroll(1000);
 assert(token);
 append(22,1020,1200); /* local acknowledgement / residual echo */
 assert(s_preroll.count==0); /* must not become remote AI input */
 assert(!starter_preroll_bind(&s_preroll,token,7,1200)); /* fast server */
 return 0;
}
''', [COMMON/'include'])

    def test_network_request_starts_before_ack_completion(self):
        source=(ROOT/'platforms/esp-idf/components/starter_runtime/src/starter_runtime.c').read_text()
        run_c(r'''
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdatomic.h>
#include <string.h>
#include "xiaotai_ai_feedback.h"
#define ESP_OK 0
#define ESP_ERR_INVALID_STATE -1
#define ESP_ERR_INVALID_ARG -2
#define ESP_ERR_TIMEOUT -3
#define ESP_LOGI(...) ((void)0)
#define ESP_LOGW(...) ((void)0)
#define ESP_LOGE(...) ((void)0)
#define STARTER_RUNTIME_WAITING 0
#define STARTER_RUNTIME_H5_ACTIVE 1
#define STARTER_RUNTIME_AI_CONNECTING 2
#define STARTER_RUNTIME_AI_ACTIVE 3
#define STARTER_RUNTIME_ROOM_CONNECTING 8
#define STARTER_RUNTIME_ROOM_ACTIVE 9
#define XIAOTAI_OWNER_AI 2
#define STARTER_AI_UI_LISTENING 1
#define PLATFORM_SERVICE_AI 4
#define AI_READY_WAIT_TIMEOUT_MS 15000
#define AI_REQUEST_TIMEOUT_MS 17000
#define EVENT_AI_START 1
typedef int starter_runtime_state_t;
typedef int esp_err_t;
typedef struct {int type;uint32_t generation;} runtime_event_t;
static runtime_event_t queued;
static bool s_ai_start_pending,s_room_page_active;
static int64_t s_ai_ready_deadline_ms,s_diagnostic_ai_started_ms;
static uint32_t s_connection_generation;
static atomic_int s_last_error;
static int s_session,state,requests;
static bool ack_pending=true;
static int64_t now_ms(void){return 1000;}
static int session_state(void){return state;}
static uint32_t session_generation(void){return 9;}
static bool platform_client_ready(void){return true;}
static bool starter_tirtc_started(void){return true;}
static struct {bool microphone_muted;} starter_media_status(void){return (__typeof__(starter_media_status())){false};}
static void starter_media_cancel_ai_preroll(uint32_t t){(void)t;assert(0);}
static void product_set_ai_start_pending(bool b,const char *s){(void)b;(void)s;}
static void room_set_foreground(bool b){(void)b;}
static void starter_tirtc_accept_h5(bool b){assert(!b);}
static void starter_media_set_wake_allowed(bool b){assert(!b);}
static int starter_media_stop_for_ai(uint32_t t){assert(t==42 && ack_pending);return 0;}
static bool starter_tirtc_connected(void){return false;}
static int starter_tirtc_disconnect(void){assert(0);return 0;}
static bool xiaotai_runtime_begin(int *s,int owner,bool incoming){assert(s==&s_session && owner==2 && !incoming);state=2;return true;}
static void arm_session_timeout(int ms){assert(ms==17000);}
static void product_snapshot_reset(void){}
static void product_set_phase(int phase){assert(phase==1);}
static void publish_state(void){}
static void diagnostic_event(const char *s,int v){(void)s;(void)v;}
static void request_ai_token_response(const char *body,void *ctx){(void)body;(void)ctx;}
static int platform_client_request_timeout(int service,const char *url,void *body,unsigned timeout,void (*cb)(const char*,void*),void *ctx){
 assert(ack_pending && service==4 && strcmp(url,"/v1/ai/token")==0);
 assert(!body && timeout==15000 && cb==request_ai_token_response && (uintptr_t)ctx==9);
 requests++;return 0; /* enqueue only: remote callback has not run */
}
static void finish_session(int err){(void)err;assert(0);}
static bool queue_event(const runtime_event_t *e){queued=*e;return true;}
''' + function(source,'starter_runtime_ai_start_from_wake') +
              function(source,'begin_ai_session') + r'''
int main(void){
 assert(starter_runtime_ai_start_from_wake(42)==0 && queued.type==EVENT_AI_START);
 begin_ai_session(queued.generation);
 assert(requests==1 && state==2 && ack_pending);
 return 0;
}
''', [ROOT/'product/include'])

    def test_ack_completion_tail_and_runtime_admission(self):
        runtime=(ROOT/'platforms/esp-idf/components/starter_runtime/src/starter_runtime.c').read_text()
        for path in MEDIA:
            with self.subTest(board=path.parts[-5]):
                source=path.read_text()
                api=''.join(function(source,name) for name in (
                    'starter_media_prepare_ai_preroll','starter_media_begin_ai_ack',
                    'starter_media_finish_ai_ack','starter_media_ai_input_ready',
                    'starter_media_ai_preroll_failed'))
                run_c(PREFIX + r'''
static uint32_t s_expected_wake_token;
''' + (COMMON/'src/starter_preroll.c').read_text() + api + r'''
#define STARTER_RUNTIME_AI_CONNECTING 2
#define STARTER_TIRTC_AI 2
#define STARTER_AI_UI_LISTENING 1
#define ESP_OK 0
#define ESP_ERR_INVALID_STATE -1
#define ESP_ERR_INVALID_ARG -2
#define ESP_ERR_TIMEOUT -3
static bool s_ai_media_pending;
static unsigned s_connection_generation=7;
static int s_session,state=2,starts,errors,published;
static int64_t s_diagnostic_ai_started_ms;
static int session_state(void){return state;}
static unsigned session_generation(void){return 9;}
static int64_t now_ms(void){return now;}
static int starter_media_start(int mode,unsigned gen){
 assert(mode==2 && gen==7 && starter_media_ai_input_ready());
 assert(starter_preroll_bind(&s_preroll,s_expected_wake_token,gen,now));
 starts++;return 0;
}
static void finish_session(int err){(void)err;errors++;}
static int xiaotai_runtime_media_started(int *s,unsigned gen){assert(s==&s_session && gen==9);state=3;return 0;}
static void product_set_phase(int phase){assert(phase==1);}
static void publish_state(void){published++;}
static void diagnostic_event(const char *s,int value){(void)s;(void)value;}
''' + function(runtime,'service_ai_media_admission') + r'''
int main(void){
 starter_preroll_init(&s_preroll,memory,160000);
 append(11,20,1000);
 unsigned token=starter_media_prepare_ai_preroll(1000);
 s_expected_wake_token=token;
 assert(token && !starter_media_ai_input_ready());
 assert(!starter_media_begin_ai_ack(token+1));
 now=1100;assert(starter_media_begin_ai_ack(token));
 assert(!starter_media_begin_ai_ack(token)); /* only one playback */
 s_ai_media_pending=true; /* server accepts while acknowledgement is playing */
 service_ai_media_admission();assert(starts==0 && s_ai_media_pending);
 append(22,1020,2200);
 assert(s_preroll.count==0 && !starter_media_ai_input_ready());
 now=2200;starter_media_finish_ai_ack(token+1);
 assert(!starter_media_ai_input_ready());
 starter_media_finish_ai_ack(token);
 append(22,2220,2360); /* tail / capture frame straddling boundary */
 service_ai_media_admission();assert(starts==0);
 append(33,2380,2600); /* user speech buffered while network is not ready */
 now=2600;assert(starter_media_ai_input_ready());
 assert(s_preroll.count==1920 && s_preroll.timestamp_ms==2360);
 s_ai_media_pending=false;service_ai_media_admission();assert(starts==0);
 s_ai_media_pending=true;service_ai_media_admission();
 assert(starts==1 && published==1 && state==3 && errors==0);
 service_ai_media_admission();assert(starts==1);
 int16_t out[160];unsigned timestamp;
 assert(starter_preroll_peek(&s_preroll,7,out,160,&timestamp) && timestamp==2360);
 for(int i=0;i<160;i++)assert(out[i]==33);
 /* Cancelled session and old completion cannot release the next wake. */
 starter_preroll_clear(&s_preroll);append(11,2620,2800);now=2800;
 unsigned next=starter_media_prepare_ai_preroll(2800);assert(next!=token);
 s_expected_wake_token=next;
 starter_media_finish_ai_ack(token);assert(!starter_media_ai_input_ready());
 assert(!starter_media_begin_ai_ack(token));
 state=0;s_ai_media_pending=true;service_ai_media_admission();
 assert(!s_ai_media_pending && starts==1);
 /* Missing worker fails closed after a bounded wait. */
 now=4801;assert(starter_media_ai_preroll_failed());
 assert(!starter_media_begin_ai_ack(next));
 starter_media_finish_ai_ack(next);assert(!starter_media_ai_input_ready());
 /* Muted/output-busy prompt can explicitly finish and admit new speech. */
 starter_preroll_clear(&s_preroll);append(11,4820,5000);now=5000;
 next=starter_media_prepare_ai_preroll(5000);s_expected_wake_token=next;
 assert(starter_media_begin_ai_ack(next));starter_media_finish_ai_ack(next);
 append(22,5020,5160);assert(!starter_media_ai_input_ready());
 append(33,5180,5200);assert(starter_media_ai_input_ready());
 /* A stalled playback does not expose its echo after a timeout. */
 starter_preroll_clear(&s_preroll);append(11,5220,5400);now=5400;
 next=starter_media_prepare_ai_preroll(5400);s_expected_wake_token=next;
 assert(starter_media_begin_ai_ack(next));now=10401;
 assert(starter_media_ai_preroll_failed());
 starter_media_finish_ai_ack(next);assert(!starter_media_ai_input_ready());
 s_expected_wake_token=0;assert(starter_media_ai_input_ready()); /* manual start */
 return 0;
}
''', [COMMON/'include'])

if __name__=='__main__': unittest.main()
