"""AI TAG provenance through the real command admission and product parser."""
import subprocess
import tempfile
import unittest
from pathlib import Path

from test_s3_product_regressions import ROOT, function, run_c


class AiFaceTrace(unittest.TestCase):
    def test_snapshot_sync_reports_lock_failure_without_changing_state(self):
        source = (ROOT / 'platforms/esp-idf/components/starter_runtime/src/starter_runtime.c').read_text()
        run_c(r'''
#include <assert.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
static void *s_product_mutex;
static bool lock_ok;
static unsigned takes,gives;
static struct {char emotion[16];bool emotion_tag_present;} s_product_snapshot={"happy",true};
#define pdTRUE 1
#define pdMS_TO_TICKS(n) (n)
static int xSemaphoreTake(void *mutex,unsigned wait){takes++;return lock_ok;}
static void xSemaphoreGive(void *mutex){gives++;}
''' + function(source, 'supported_emotion') + function(source, 'product_set_emotion') + r'''
int main(void){
    assert(!product_set_emotion("neutral",false) && !takes && !gives);
    s_product_mutex=(void*)1;
    assert(!product_set_emotion("neutral",false) && takes==1 && !gives);
    assert(s_product_snapshot.emotion_tag_present && !strcmp(s_product_snapshot.emotion,"happy"));
    lock_ok=true;
    assert(!product_set_emotion("invalid",false) && takes==1 && !gives);
    assert(product_set_emotion("neutral",false) && takes==2 && gives==1);
    assert(!s_product_snapshot.emotion_tag_present && !strcmp(s_product_snapshot.emotion,"neutral"));
}
''')

    def test_command_trace_distinguishes_received_retained_and_cleared_tags(self):
        source = (ROOT / 'platforms/esp-idf/components/starter_runtime/src/starter_runtime.c').read_text()
        # Keep production generation/JSON/session admission and AI view handling;
        # the subsequent caption rendering, call tools and teardown are separate.
        handler = function(source, 'handle_ai_command').split(
            '        const cJSON *method = cJSON_GetObjectItemCaseSensitive(root, "method");')[0]
        handler += '        cJSON_Delete(root);\n    } else { cJSON_Delete(root); }\n}\n'
        code = r'''
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <stdarg.h>
#include <cJSON.h>
#include "xiaotai_ai_view.h"
#define STARTER_TIRTC_AI 2
#define AI_COMMAND 0x2100
#define STARTER_RUNTIME_AI_CONNECTING 2
#define STARTER_RUNTIME_AI_ACTIVE 3
#define ESP_FAIL -1
#define ESP_ERR_NOT_SUPPORTED 262
#define TAG "test"
typedef int starter_runtime_state_t;
typedef struct { unsigned mode,command,generation; const char *text; size_t length; } runtime_event_t;
static unsigned s_connection_generation=7;
static char s_ai_request_id[]="private-request";
static bool s_ai_media_pending;
static xiaotai_ai_view_t s_ai_view;
static int state=STARTER_RUNTIME_AI_ACTIVE;
static unsigned clock_ms=100, syncs;
static bool sync_ok=true, ui_tag;
static char logs[8192];
static void log_msg(const char *fmt,...){
    va_list args;va_start(args,fmt);
    size_t n=strlen(logs);vsnprintf(logs+n,sizeof(logs)-n,fmt,args);va_end(args);
}
#define ESP_LOGI(tag,...) log_msg(__VA_ARGS__)
#define ESP_LOGE(tag,...) log_msg(__VA_ARGS__)
static void diagnostic_event(const char *label,int value){}
static starter_runtime_state_t session_state(void){return state;}
static int64_t now_ms(void){return clock_ms;}
static void finish_session(int err){assert(0);}
static bool starter_media_ai_input_ready(void){return true;}
static void service_ai_media_admission(void){}
static bool ai_audio_profile_valid(const cJSON *v,bool input){return true;}
static void ai_log_start_response(const cJSON *v){}
static bool product_set_emotion(const char *emotion,bool present){
    syncs++;if(sync_ok)ui_tag=present;return sync_ok;
}
static int product_phase_from_ai_view(int phase){return phase;}
static void product_set_phase(int phase){}
''' + function(source, 'ai_face_input_emotion') + function(source, 'ai_face_apply_event') + handler + r'''
static void receive(const char *json){
    logs[0]=0;
    runtime_event_t event={2,AI_COMMAND,7,json,strlen(json)};
    handle_ai_command(&event);
    assert(!strstr(logs,"private") && !strstr(logs,"SECRET"));
    clock_ms+=100;
}
int main(void){
    xiaotai_ai_view_init(&s_ai_view);
    receive("{\"method\":\"round_start\"}");
    assert(strstr(logs,"event=round_start") && strstr(logs,"input=none"));
    assert(strstr(logs,"tag=0->0") && !s_ai_view.emotion_tag_present);
    receive("{\"method\":\"caption\",\"params\":{\"caption_type\":1,\"utterance_id\":\"private-intro\",\"text\":\"SECRET intro\",\"emotion\":\"开心\",\"is_final\":true}}");
    assert(strstr(logs,"event=caption") && strstr(logs,"input=happy") && strstr(logs,"group=new"));
    assert(strstr(logs,"tag=0->1") && strstr(logs,"sync=1") && ui_tag);
    // User music trace: completed untagged captions reuse the intro's group.
    receive("{\"method\":\"caption\",\"params\":{\"caption_type\":1,\"utterance_id\":\"private-intro\",\"text\":\"SECRET song\",\"is_final\":true}}");
    assert(strstr(logs,"group=same") && strstr(logs,"input=none"));
    assert(strstr(logs,"tag=1->0") && strstr(logs,"sync=1") && !ui_tag);
    receive("{\"method\":\"caption\",\"params\":{\"caption_type\":1,\"utterance_id\":\"private-intro\",\"text\":\"SECRET song next\",\"is_final\":true}}");
    assert(strstr(logs,"tag=0->0") && !ui_tag);
    receive("{\"method\":\"caption\",\"params\":{\"caption_type\":1,\"utterance_id\":\"private-intro\",\"text\":\"SECRET explicit neutral\",\"emotion\":\"neutral\",\"is_final\":true}}");
    assert(strstr(logs,"input=neutral") && strstr(logs,"tag=0->1") && ui_tag);
    // Reassert happy, then repeat it: even an unchanged partial TAG is new wire evidence.
    receive("{\"method\":\"caption\",\"params\":{\"caption_type\":1,\"utterance_id\":\"private-intro\",\"text\":\"SECRET partial\",\"emotion\":\"happy\"}}");
    receive("{\"method\":\"caption\",\"params\":{\"caption_type\":1,\"utterance_id\":\"private-intro\",\"text\":\"SECRET partial repeat\",\"emotion\":\"happy\"}}");
    assert(strstr(logs,"input=happy") && strstr(logs,"tag=1->1"));
    // Partial -> final in one segment: keep the face, but do not label it received.
    receive("{\"method\":\"caption\",\"params\":{\"caption_type\":1,\"utterance_id\":\"private-intro\",\"text\":\"SECRET tail\",\"is_final\":true}}");
    assert(strstr(logs,"input=none") && strstr(logs,"tag=1->1") && strstr(logs,"group=same"));
    receive("{\"method\":\"round_end\"}");
    assert(strstr(logs,"event=round_end") && strstr(logs,"tag=1->1"));
    receive("{\"method\":\"round_start\"}");
    assert(strstr(logs,"tag=1->0") && !s_ai_view.emotion_tag_present && !ui_tag);
    receive("{\"method\":\"emotion\",\"params\":{\"tag\":\"neutral\"}}");
    assert(strstr(logs,"input=neutral") && strstr(logs,"pending=1") && ui_tag);
    receive("{\"method\":\"caption\",\"params\":{\"caption_type\":1,\"utterance_id\":\"private-song\",\"text\":\"SECRET song\",\"is_final\":true}}");
    assert(strstr(logs,"input=none") && strstr(logs,"tag=1->1") && strstr(logs,"pending=0"));
    receive("{\"method\":\"caption\",\"params\":{\"caption_type\":1,\"utterance_id\":\"private-next\",\"text\":\"SECRET next\",\"is_final\":true}}");
    assert(strstr(logs,"tag=1->0") && !ui_tag);
    // Unknown payload is not echoed or treated as an accepted emotion.
    receive("{\"method\":\"event\",\"params\":{\"data\":{\"emotion\":\"SECRET unknown\"}}}");
    assert(strstr(logs,"input=invalid") && !s_ai_view.emotion_tag_present);
    receive("{\"method\":\"event\",\"params\":{\"data\":{\"tag\":\"sad\"}}}");
    assert(strstr(logs,"input=sad") && s_ai_view.emotion_tag_present);
    receive("{\"method\":\"caption\",\"params\":{\"emotion\":\"happy\",\"is_final\":true}}");
    assert(strstr(logs,"valid=0") && strstr(logs,"sync=-1") && !strcmp(s_ai_view.emotion,"sad"));
    receive("{\"method\":\"expression\",\"params\":{\"emotion_tag\":\"惊讶\"}}");
    assert(strstr(logs,"input=surprised") && !strcmp(s_ai_view.emotion,"surprised"));
    receive("{\"type\":\"AI_EMOTION\",\"expression\":\"cool\"}");
    assert(strstr(logs,"event=AI_EMOTION") && strstr(logs,"input=cool"));
    receive("{\"type\":\"ai_reply\",\"text\":\"SECRET old\",\"emotion\":\"happy\"}");
    assert(strstr(logs,"event=ai_reply") && strstr(logs,"input=happy"));
    // A synchronization failure must not masquerade as a successful UI update.
    sync_ok=false;
    receive("{\"method\":\"round_start\"}");
    assert(strstr(logs,"tag=1->0") && strstr(logs,"sync=0") && ui_tag);
    sync_ok=true;
    receive("{\"method\":\"round_start\"}");
    assert(!ui_tag);
    // Partial captions in one group are coalesced, without altering throttling.
    receive("{\"method\":\"caption\",\"params\":{\"caption_type\":1,\"utterance_id\":\"private-partial\",\"text\":\"SECRET A\"}}");
    clock_ms-=99;
    unsigned before=syncs;
    receive("{\"method\":\"caption\",\"params\":{\"caption_type\":1,\"utterance_id\":\"private-partial\",\"text\":\"SECRET B\"}}");
    assert(!logs[0] && syncs==before);
    receive("{\"method\":\"SECRET method\",\"params\":{\"emotion\":\"happy\"}}");
    assert(!logs[0] && !s_ai_view.emotion_tag_present);
    // Generation and JSON guards must run before tracing or updating the view.
    const char *json="{\"method\":\"emotion\",\"params\":{\"tag\":\"happy\"}}";
    runtime_event_t stale={2,AI_COMMAND,6,json,strlen(json)};
    logs[0]=0;handle_ai_command(&stale);assert(!logs[0] && !s_ai_view.emotion_tag_present);
    receive("not JSON");assert(!logs[0] && !s_ai_view.emotion_tag_present);
}
'''
        with tempfile.TemporaryDirectory(prefix='ai-face-trace-') as tmp:
            path = Path(tmp)
            (path / 'test.c').write_text(code)
            subprocess.run(['cc', '-std=c11', '-Wall', '-Werror', '-Wno-unused-function',
                            '-I/usr/include/cjson', f'-I{ROOT / "product/include"}',
                            str(path / 'test.c'), str(ROOT / 'product/src/xiaotai_ai_view.c'),
                            '-lcjson', '-o', str(path / 'test')], check=True)
            subprocess.run([str(path / 'test')], check=True)


if __name__ == '__main__':
    unittest.main()
