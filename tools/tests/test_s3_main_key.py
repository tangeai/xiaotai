"""Replay the physical main key against the serialized ESP runtime policy."""
import unittest
import subprocess
from test_s3_product_regressions import ROOT, function, run_c

RUNTIME = ROOT / 'platforms/esp-idf/components/starter_runtime/src/starter_runtime.c'
BUTTON = ROOT / 'platforms/esp-idf/components/starter_button/src/starter_button.c'


class MainKeyRegressions(unittest.TestCase):
    def test_key_and_home_share_first_wechat_and_missing_contact_refresh(self):
        source=RUNTIME.read_text()
        run_c(r'''
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include <string.h>
#define CONFIG_IDF_TARGET_ESP32P4 0
#define pdTRUE 1
#define pdMS_TO_TICKS(x) (x)
#define STARTER_CONTACT_WECHAT 2
#define STARTER_PRODUCT_CONTACTS_MAX 4
#define CONTACTS_CHECK_TIMEOUT_MS 1000
static void *s_product_mutex=(void *)1;
static bool lock_ok=true,s_wechat_quick_pending;
static struct {uint8_t contact_count;uint32_t contact_guide_sequence;bool wechat_contacts_checked;
 struct {int source;} contacts[4];} s_product_snapshot;
static unsigned s_contacts_check_deadline_ms,s_contacts_retry_due_ms;
static int dials,ends,refreshes;
static int xSemaphoreTake(void *mutex,int timeout){return lock_ok;}
static void xSemaphoreGive(void *mutex){}
static unsigned now_ms(void){return 100;}
static void refresh_contacts(void){refreshes++;}
static void end_ai_session(void){ends++;}
static void dial_contact(uint8_t index,bool video){assert(index==1 && !video);dials++;}
''' + function(source,'call_video_for_request') +
            function(source,'handle_wechat_quick_call') + function(source,'dial_first_contact') + r'''
int main(void){
 s_product_snapshot.contact_count=4;
 s_product_snapshot.contacts[0].source=1;s_product_snapshot.contacts[1].source=2;
 s_product_snapshot.contacts[2].source=1;s_product_snapshot.contacts[3].source=2;
 dial_first_contact();handle_wechat_quick_call();assert(dials==2 && refreshes==0);
 s_product_snapshot.contact_count=1;dial_first_contact();
 assert(dials==2 && s_wechat_quick_pending && refreshes==1);
 s_product_snapshot.contact_count=0;dial_first_contact();assert(dials==2 && refreshes==2);
 assert(s_contacts_check_deadline_ms==1100 && s_contacts_retry_due_ms==100);
}
''')

    def test_missing_wechat_guide_is_presented_once_and_does_not_interrupt_calls(self):
        ui=(ROOT/'platforms/esp-idf/components/starter_product/src/starter_product.c').read_text()
        start=ui.index('    if (product.contact_guide_sequence != s_contact_guide_sequence)')
        end=ui.index('    home =',start)
        run_c(r'''
#include <assert.h>
#include <stdint.h>
#include <stdbool.h>
#define PAGE_WECHAT_QR 5
static struct {uint32_t contact_guide_sequence;} product;
static uint32_t s_contact_guide_sequence;
static int s_page,renders;
static bool call_now,binding_required;
static void render_page(void){renders++;}
static void tick(void){
''' + ui[start:end] + r'''
}
int main(void){
 product.contact_guide_sequence=1;tick();assert(s_page==PAGE_WECHAT_QR && renders==1);
 s_page=0;tick();assert(s_page==0 && renders==1);
 product.contact_guide_sequence=2;call_now=true;tick();assert(s_page==0 && renders==1);
 call_now=false;tick();assert(s_page==0 && renders==1);
 product.contact_guide_sequence=3;binding_required=true;tick();assert(renders==1);
}
''')

    def test_prototype_home_calls_first_wechat_not_first_device(self):
        script=(ROOT/'docs/product/prototype.js').read_text()
        code = """const assert=require('assert');
let list=[{name:'device',source:'设备'},{name:'wx1',source:'微信'},{name:'wx2',source:'微信'}];
const visibleContacts=()=>list;let peer,callMode,callMicMuted,cameraOff,state;
function render(next){state=next;}
""" + function(script,'quickFirstContactCall') + """
quickFirstContactCall();assert.equal(peer.name,'wx1');assert.equal(state,'dialing');
list=[{name:'device',source:'设备'}];quickFirstContactCall();assert.equal(state,'wechat-auth');
list=[];quickFirstContactCall();assert.equal(state,'wechat-auth');
"""
        subprocess.run(['node','-e',code],check=True)

    def test_polling_handles_boot_hold_bounce_double_click_and_queue_failure(self):
        source=BUTTON.read_text()
        run_c(r'''
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include <setjmp.h>
#define BUTTON_POLL_MS 10U
#define BUTTON_DEBOUNCE_MS 50U
#define BUTTON_DOUBLE_CLICK_MS 300U
#define ESP_OK 0
#define ESP_LOGW(...) ((void)0)
#define pdMS_TO_TICKS(x) (x)
typedef int esp_err_t;
static bool input[256];
static unsigned tick,limit,calls,doubles;
static int queue_error;
static jmp_buf done;
static int starter_runtime_main_key(bool twice){calls++;doubles+=twice;return queue_error;}
static bool read_pressed(void *context){return input[tick];}
static struct {void *context;bool (*read_pressed)(void *);} adapter={0,read_pressed},*s_button=&adapter;
static void vTaskDelay(unsigned delay){assert(delay==10);if(++tick>=limit)longjmp(done,1);}
''' + function(source,'post_main_key') + function(source,'button_task') + r'''
static void replay(void){tick=calls=doubles=0;limit=250;if(setjmp(done)==0)button_task(NULL);}
static void hold(unsigned start,unsigned end){for(unsigned i=start;i<=end;i++)input[i]=true;}
int main(void){
    hold(0,30);hold(51,70);replay();assert(calls==1 && doubles==0);
    memset(input,0,sizeof(input));for(unsigned i=1;i<30;i+=2)input[i]=true;
    replay();assert(calls==0);
    memset(input,0,sizeof(input));hold(40,49);hold(60,69);
    replay();assert(calls==1 && doubles==1);
    memset(input,0,sizeof(input));hold(10,100);hold(180,200);
    replay();assert(calls==2 && doubles==0);
    memset(input,0,sizeof(input));hold(10,100);queue_error=-1;
    replay();assert(calls==1 && doubles==0);
}
''')

    def test_main_key_resolves_pending_incoming_before_current_media(self):
        source = RUNTIME.read_text()
        try:
            policy = function(source, 'handle_main_key')
        except AssertionError:
            # Exercise the current adapter implementation before moving the
            # decision into the state-owning task.
            policy = function(BUTTON.read_text(), 'toggle_ai') + '\nstatic void handle_main_key(bool twice){toggle_ai();}'
        run_c(r'''
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#define ESP_OK 0
#define ESP_LOGI(...) ((void)0)
#define ESP_LOGD(...) ((void)0)
#define ESP_LOGW(...) ((void)0)
#define STARTER_RUNTIME_WAITING 0
#define STARTER_RUNTIME_H5_ACTIVE 1
#define STARTER_RUNTIME_AI_CONNECTING 2
#define STARTER_RUNTIME_AI_ACTIVE 3
#define STARTER_RUNTIME_CALL_INCOMING 4
#define STARTER_RUNTIME_CALL_CONNECTING 5
#define STARTER_RUNTIME_CALL_ACTIVE 6
#define STARTER_RUNTIME_CALL_ENDING 7
#define XIAOTAI_STATE_CALL_ENDING 7
#define STARTER_RUNTIME_ROOM_CONNECTING 8
#define STARTER_RUNTIME_ROOM_ACTIVE 9
#define STARTER_ROOM_ASSIGNED 1
#define STARTER_ROOM_NONE 0
typedef int starter_runtime_state_t;
typedef int esp_err_t;
typedef struct {int state;} starter_runtime_status_t;
static int state,starts,ends,accepts,rejects,hangups,exits,dials;
static int guides;
static bool contacts_available=true;
static bool pending,s_room_page_active;
bool s_room_desired=true;
static int session_state(void){return state;}
static bool session_incoming_pending(void){return pending;}
static starter_runtime_status_t starter_runtime_status(void){return (starter_runtime_status_t){state};}
static int starter_runtime_ai_start(void){starts++;return 0;}
static int starter_runtime_ai_stop(void){ends++;return 0;}
static void begin_ai_session(uint32_t token){assert(token==0);starts++;}
static void end_ai_session(void){ends++;}
static void accept_call(void){accepts++;}
static void reject_or_hangup_call(bool reject){if(reject)rejects++;else hangups++;}
static void room_set_foreground(bool active){assert(!active);s_room_page_active=false;exits++;}
static void product_set_room(int phase,const char *message){}
static void dial_contact(uint8_t index,bool video){assert(index==0 && !video);dials++;}
static void dial_first_contact(void){if(contacts_available)dial_contact(0,false);else guides++;}
''' + policy + r'''
static void reset(void){starts=ends=accepts=rejects=hangups=exits=dials=0;pending=false;s_room_page_active=false;guides=0;contacts_available=true;}
int main(void){
    for(int media=0;media<=9;media++){
        reset();state=media;pending=true;handle_main_key(false);
        assert(accepts==1 && starts==0 && ends==0 && exits==0 && hangups==0);
        handle_main_key(true);assert(rejects==1 && dials==0);
    }
    reset();state=STARTER_RUNTIME_CALL_ACTIVE;handle_main_key(false);assert(hangups==1 && starts==0);
    handle_main_key(true);assert(dials==0);
    reset();state=STARTER_RUNTIME_AI_ACTIVE;handle_main_key(false);assert(ends==1);
    handle_main_key(true);assert(dials==1);
    contacts_available=false;dials=0;handle_main_key(true);assert(guides==1 && dials==0);
    reset();state=STARTER_RUNTIME_ROOM_ACTIVE;s_room_page_active=true;
    handle_main_key(false);assert(exits==1 && starts==0);
    reset();state=STARTER_RUNTIME_WAITING;s_room_page_active=true;
    handle_main_key(false);assert(exits==1 && starts==0);
    reset();state=STARTER_RUNTIME_WAITING;handle_main_key(false);assert(starts==1);
    handle_main_key(true);assert(dials==1);
    reset();state=STARTER_RUNTIME_CALL_ENDING;handle_main_key(false);handle_main_key(true);
    assert(starts==0 && hangups==0 && dials==0);
}
''')


if __name__ == '__main__':
    unittest.main()
