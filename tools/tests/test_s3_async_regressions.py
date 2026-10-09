"""Replay ESP SDK and UI failure boundaries using production C snippets."""
from pathlib import Path
import unittest

from test_s3_product_regressions import ROOT, function, run_c

RUNTIME = ROOT / "platforms/esp-idf/components/starter_runtime/src/starter_runtime.c"
UI = ROOT / "platforms/esp-idf/components/starter_product/src/starter_product.c"


class S3AsyncRegressions(unittest.TestCase):
    def test_room_media_gate_is_independent_for_s3_and_p4(self):
        paths=[ROOT / 'firmware/esp-idf/esp32s3/lckfb-esp32s3-xiaotai',
               ROOT / 'firmware/esp-idf/esp32p4/waveshare-touch-lcd-43c']
        for path in paths:
            with self.subTest(board=path.name):
                source=(path / 'components/starter_media/src/starter_media.c').read_text()
                run_c(r'''
#include <assert.h>
#include <stdbool.h>
#include <stdatomic.h>
typedef int starter_tirtc_mode_t;
#define STARTER_TIRTC_ROOM 5
static atomic_bool s_uplink_enabled,s_room_pressed;
''' + function(source,'uplink_allowed') +
                      function(source,'starter_media_set_room_pressed') +
                      function(source,'starter_media_set_uplink_enabled') + r'''
int main(void){
    starter_media_set_room_pressed(true);
    assert(!uplink_allowed(STARTER_TIRTC_ROOM));
    starter_media_set_uplink_enabled(true);
    assert(uplink_allowed(STARTER_TIRTC_ROOM));
    for(int i=0;i<100;i++){
        starter_media_set_room_pressed(false);
        assert(!uplink_allowed(STARTER_TIRTC_ROOM));
        for(int mode=0;mode<5;mode++)assert(uplink_allowed(mode));
        starter_media_set_room_pressed(true);assert(uplink_allowed(STARTER_TIRTC_ROOM));
    }
    starter_media_set_uplink_enabled(false);
    for(int mode=0;mode<=5;mode++)assert(!uplink_allowed(mode));
}
''')

    def test_stale_ptt_release_cannot_disable_ai_uplink(self):
        source=RUNTIME.read_text()
        run_c(r'''
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdatomic.h>
#define ESP_OK 0
#define ESP_ERR_TIMEOUT -1
#define EVENT_ROOM_ACTION 1
#define ROOM_ACTION_PTT 5
typedef int esp_err_t;
typedef struct {int type,command;bool flag;} runtime_event_t;
static atomic_bool s_room_release_required;
atomic_bool s_room_key_pressed;
static bool ai_uplink=true,room_uplink=true;
static void starter_media_set_uplink_enabled(bool enabled){ai_uplink=enabled;room_uplink=enabled;}
static void starter_media_set_room_pressed(bool on){room_uplink=on;}
static bool queue_event(const runtime_event_t *event){return false;}
''' + function(source,'starter_runtime_room_ptt') + r'''
int main(void){
    assert(starter_runtime_room_ptt(false)==ESP_ERR_TIMEOUT);
    assert(ai_uplink && !room_uplink && atomic_load(&s_room_release_required));
}
''')

    def test_ptt_press_fails_closed_when_control_send_fails(self):
        source=RUNTIME.read_text()
        handler=function(source,'handle_room_action')
        start=handler.index('    if (event->command == ROOM_ACTION_PTT)')
        end=handler.index('    if (event->command == s_room_mutation_action',start)
        run_c(r'''
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <stdatomic.h>
#define ROOM_ACTION_PTT 5
#define ROOM_COMMAND 1
#define STARTER_RUNTIME_ROOM_ACTIVE 9
#define STARTER_ROOM_JOINED 3
#define ESP_LOGW(...) ((void)0)
typedef struct {int command;bool flag;} runtime_event_t;
static bool s_room_joined=true,s_room_ptt,uplink;
static atomic_bool s_room_key_pressed=true;
static int send_error,state=STARTER_RUNTIME_ROOM_ACTIVE;
static int session_state(void){return state;}
static void starter_media_set_uplink_enabled(bool on){uplink=on;}
static int starter_tirtc_send_command(int command,const char *body,uint32_t size){return send_error;}
static void product_set_room(int phase,const char *text){}
static void handle(runtime_event_t *event){
''' + handler[start:end] + r'''
}
int main(void){runtime_event_t e={.command=ROOM_ACTION_PTT,.flag=true};
    send_error=-1;handle(&e);assert(!s_room_ptt && !uplink);
    send_error=0;handle(&e);assert(s_room_ptt && uplink);
    e.flag=false;send_error=-1;handle(&e);assert(!s_room_ptt && !uplink);
    e.flag=true;s_room_key_pressed=false;send_error=0;handle(&e);
    assert(!s_room_ptt && !uplink);
}
''')

    def test_room_input_enqueue_failure_keeps_entered_digits(self):
        source = UI.read_text()
        start = source.index("action == ACTION_ROOM_INPUT_SUBMIT)")
        end = source.index("} else if (action >= ACTION_DIAG_SYSTEM", start)
        branch = "if (" + source[start:end] + "}"
        run_c(r'''
#include <assert.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#define ESP_OK 0
typedef int esp_err_t;
#define ACTION_ROOM_INPUT_SUBMIT 1
#define ACTION_ROOM_INPUT_SKIP 2
#define PAGE_ROOM_CODE 1
#define PAGE_ROOM_PASSWORD 2
#define PAGE_ROOM 3
static int s_page=PAGE_ROOM_PASSWORD, error=-1,calls;
static bool s_room_input_create;
static char s_room_input[16]="1234",s_room_join_code[16]="123456";
static bool room_action_submitted(int err){return err==0;}
static int starter_runtime_room_create(const char *pass){calls++;return error;}
static int starter_runtime_room_join(const char *code,const char *pass){calls++;return error;}
static void act(int action){
''' + branch + r'''
}
int main(void){
    act(ACTION_ROOM_INPUT_SUBMIT);
    assert(calls==1 && s_page==PAGE_ROOM_PASSWORD && strcmp(s_room_input,"1234")==0);
    s_room_input_create=true;act(ACTION_ROOM_INPUT_SUBMIT);
    assert(calls==2 && s_page==PAGE_ROOM_PASSWORD);
    s_room_input_create=false;s_room_input[0]='\0';act(ACTION_ROOM_INPUT_SUBMIT);
    assert(calls==3 && s_page==PAGE_ROOM_PASSWORD);
    error=0;act(ACTION_ROOM_INPUT_SUBMIT);assert(calls==4 && s_page==PAGE_ROOM);
}
''')

    def test_room_leave_invalidates_pending_connect_token(self):
        source=RUNTIME.read_text()
        handler=function(source,"handle_room_action")
        start=handler.index("    if (event->command == ROOM_ACTION_LEAVE)")
        end=handler.index("    if (event->text == NULL)",start)
        run_c(r'''
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
typedef struct {int command;} runtime_event_t;
#define ROOM_ACTION_LEAVE 4
#define ROOM_HTTP_MUTATION 2
static bool s_room_resume_pending, s_room_page_active=true;
static uint32_t s_room_page_epoch=2,s_room_token_epoch=2;
static int s_room_pending_action,s_room_mutation_action;
static int64_t s_room_assignment_version=3;
static char s_room_id[65]="room",s_room_pending_body[320];
static void room_stop_connection(const char *p,int err){}
static bool room_request(int stage,const char *path,const char *body){return false;}
''' + function(source,"room_token_is_current") + '\nstatic void handle(runtime_event_t *event){\n' + handler[start:end] + r'''
}
int main(void){runtime_event_t e={.command=ROOM_ACTION_LEAVE};
    assert(room_token_is_current());handle(&e);
    assert(!room_token_is_current() && s_room_pending_action==ROOM_ACTION_LEAVE);
}
''')

    def test_ai_start_invalidates_pending_room_token(self):
        source = RUNTIME.read_text()
        start = function(source, "begin_ai_session")
        start = start[:start.index("    starter_tirtc_accept_h5(false);")] + "}"
        run_c('#include \"' + str(ROOT / 'product/include/xiaotai_ai_feedback.h') + '\"\n' + r'''
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#define ESP_LOGI(...) ((void)0)
#define ESP_LOGW(...) ((void)0)
#define STARTER_RUNTIME_WAITING 0
#define STARTER_RUNTIME_H5_ACTIVE 1
#define STARTER_RUNTIME_AI_CONNECTING 2
#define STARTER_RUNTIME_AI_ACTIVE 3
#define STARTER_RUNTIME_ROOM_CONNECTING 8
#define STARTER_RUNTIME_ROOM_ACTIVE 9
#define AI_READY_WAIT_TIMEOUT_MS 15000
typedef int starter_runtime_state_t;
static int state;
static bool s_room_page_active=true, s_ai_start_pending;
static int64_t s_ai_ready_deadline_ms;
static uint32_t s_room_page_epoch=2, s_room_token_epoch=2;
static bool ready=true, muted;
static int closes;
static int64_t now_ms(void){return 100;}
static int session_state(void){return state;}
static bool platform_client_ready(void){return ready;}
static bool starter_tirtc_started(void){return ready;}
typedef struct {bool microphone_muted;} media_status_t;
static media_status_t starter_media_status(void){return (media_status_t){muted};}
static void starter_media_cancel_ai_preroll(uint32_t token){}
static void product_set_ai_start_pending(bool pending,const char *text){}
static void room_set_foreground(bool active){
    assert(!active);s_room_page_active=false;s_room_page_epoch++;closes++;
}
''' + function(source, "room_token_is_current") + start + r'''
int main(void){
    ready=false;begin_ai_session(0);assert(closes==0 && room_token_is_current());
    ready=true;muted=true;begin_ai_session(0);assert(closes==0);
    muted=false;begin_ai_session(0);
    assert(closes==1 && !room_token_is_current());
}
''')

    def test_room_token_claims_owner_before_touching_media(self):
        source = RUNTIME.read_text()
        handler = function(source, "handle_room_http")
        start = handler.index("} else if (event->command == ROOM_HTTP_TOKEN)")
        end = handler.index("} else if (event->command == ROOM_HTTP_PRESENCE &&", start)
        token = "if (" + handler[start + len("} else if ("):end] + "}"
        run_c(r'''
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
typedef struct {int valueint;} cJSON;
typedef struct {char peer_id[65], token[65];} ai_credentials_t;
typedef struct {int command;} runtime_event_t;
#define ROOM_HTTP_TOKEN 2
#define STARTER_ROOM_ERROR 3
#define ESP_ERR_NO_MEM -1
#define ROOM_CONNECT_TIMEOUT_MS 100
#define XIAOTAI_OWNER_ROOM 5
static int s_session, s_room_heartbeat_seconds, s_room_lease_seconds;
static int stops,gates,connections,updates;
static bool allowed;
static bool room_token_is_current(void){return true;}
static bool copy_json_string(cJSON *data,const char *name,char *out,size_t n,bool req){return true;}
static cJSON *cJSON_GetObjectItemCaseSensitive(cJSON *p,const char *name){return NULL;}
static bool cJSON_IsNumber(const cJSON *p){return false;}
static void cJSON_Delete(cJSON *p){}
static void product_set_room(int phase,const char *text){updates++;}
static void starter_tirtc_accept_h5(bool active){gates++;}
static void starter_media_stop(void){stops++;}
static bool xiaotai_runtime_begin(int *s,int owner,bool incoming){return allowed;}
static bool suspend_mqtt_for_external_connect(void){return true;}
static void finish_session(int err){}
static int session_generation(void){return 1;}
static int starter_tirtc_room_connect(const char *p,const char *t,int g){connections++;return 0;}
static void arm_session_timeout(int ms){}
static void publish_state(void){}
static void resume_mqtt_after_external_connect(void){}
static void handle(runtime_event_t *event){cJSON *root=NULL,*data=NULL;
''' + token + r'''
}
int main(void){
    runtime_event_t e={.command=ROOM_HTTP_TOKEN};
    handle(&e);assert(stops==0 && gates==0 && connections==0 && updates==1);
    allowed=true;handle(&e);assert(stops==1 && gates==1 && connections==1);
}
''')

    def test_dropped_http_results_signal_recovery(self):
        source = RUNTIME.read_text()
        run_c(r'''
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdatomic.h>
#include <stddef.h>
#define EVENT_ROOM_HTTP 1
#define EVENT_CONTACTS_RESULT 2
typedef struct {int type;uint32_t command;} runtime_event_t;
static atomic_uint s_room_http_delivery_failed_stage;
static atomic_bool s_contacts_delivery_failed;
static bool delivered;
static bool room_inflight=true,contacts_inflight=true;
static unsigned room_failures,contact_failures;
static bool queue_http_result(int type,uint32_t tag,uint32_t stage,const char *body){return delivered;}
static void handle_room_http(const runtime_event_t *event){
    assert(event->type==EVENT_ROOM_HTTP && event->command==3);
    room_inflight=false;room_failures++;
}
static void handle_contacts_result(const runtime_event_t *event){
    assert(event->type==EVENT_CONTACTS_RESULT);contacts_inflight=false;contact_failures++;
}
''' + function(source, "room_http_response") + function(source, "contacts_response") +
              function(source, "recover_product_http_delivery_failures") + r'''
int main(void){
    room_http_response("{}",(void *)(uintptr_t)3);
    contacts_response("{}",NULL);
    assert(atomic_load(&s_room_http_delivery_failed_stage)==3);
    assert(atomic_load(&s_contacts_delivery_failed));
    recover_product_http_delivery_failures();
    assert(!room_inflight && !contacts_inflight && room_failures==1 && contact_failures==1);
    recover_product_http_delivery_failures();
    assert(room_failures==1 && contact_failures==1);
    delivered=true;
    room_http_response("{}",(void *)(uintptr_t)3);contacts_response("{}",NULL);
    assert(atomic_load(&s_room_http_delivery_failed_stage)==0);
    assert(!atomic_load(&s_contacts_delivery_failed));
}
''')

    def test_stale_failed_room_token_has_no_side_effects(self):
        source = RUNTIME.read_text()
        handler = function(source, "handle_room_http")
        handler = handler[:handler.index("    if (event->command == ROOM_HTTP_ASSIGNMENT)")] + "}"
        run_c(r'''
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
typedef struct {char *valuestring;} cJSON;
typedef struct {int command; char *text; int length;} runtime_event_t;
#define ROOM_HTTP_MUTATION 1
#define ROOM_HTTP_TOKEN 2
#define ROOM_HTTP_PRESENCE 3
#define STARTER_ROOM_ERROR 4
#define ESP_FAIL -1
#define ESP_LOGW(...) ((void)0)
static bool s_room_http_inflight, s_room_page_active;
static uint32_t s_room_page_epoch, s_room_token_epoch;
static int s_room_mutation_action, updates, stops;
static cJSON *cJSON_ParseWithLength(const char *s,int n){return NULL;}
static bool response_ok(cJSON *p){return false;}
static cJSON *cJSON_GetObjectItemCaseSensitive(const cJSON *p,const char *s){return NULL;}
static bool cJSON_IsObject(const cJSON *p){return false;}
static bool cJSON_IsString(const cJSON *p){return false;}
static void cJSON_Delete(cJSON *p){}
static void product_set_room(int state,const char *s){updates++;}
static void room_stop_connection(const char *s,int err){stops++;}
''' + function(source, "room_token_is_current") + handler + r'''
int main(void) {
    runtime_event_t e={.command=ROOM_HTTP_TOKEN};
    s_room_page_epoch=2; s_room_token_epoch=1;
    for(int active=0; active<=1; active++) {
        s_room_page_active=active; s_room_http_inflight=true;
        handle_room_http(&e);
        assert(!s_room_http_inflight && updates==0 && stops==0);
    }
    s_room_token_epoch=2; handle_room_http(&e);
    assert(updates==1 && stops==1);
}
''')

    def test_leave_enqueue_failure_keeps_confirmation_and_reports_error(self):
        source = UI.read_text()
        start = source.index("action == ACTION_ROOM_LEAVE)")
        end = source.index("} else if (action >= ACTION_ROOM_DIGIT_BASE", start)
        branch = "if (" + source[start:end] + "}"
        run_c(r'''
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>
#define ESP_OK 0
typedef int esp_err_t;
#define ACTION_ROOM_LEAVE 1
#define ACTION_ROOM_LEAVE_CONFIRM 2
#define ACTION_ROOM_LEAVE_CANCEL 3
#define PAGE_ROOM 1
#define PAGE_ROOM_LEAVE_CONFIRM 2
static int s_page=PAGE_ROOM, leaves, error, feedbacks;
static char s_voice_feedback[64];
static int starter_runtime_room_leave(void) { leaves++; return error; }
static int64_t monotonic_ms(void) {return 100;}
static void set_voice_feedback(const char *text,int64_t now) {
    assert(text && text[0] && now==100);feedbacks++;
}
''' + function(source, 'room_action_submitted') + r'''
static void act(int action) {
''' + branch + r'''
}
int main(void) {
    act(ACTION_ROOM_LEAVE);error=-1;
    act(ACTION_ROOM_LEAVE_CONFIRM);
    assert(s_page==PAGE_ROOM_LEAVE_CONFIRM && leaves==1 && feedbacks==1);
    error=0;act(ACTION_ROOM_LEAVE_CONFIRM);
    assert(s_page==PAGE_ROOM && leaves==2);
    act(ACTION_ROOM_LEAVE_CONFIRM);assert(leaves==2);
}
''')


if __name__ == "__main__":
    unittest.main()
