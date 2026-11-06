"""Replay production BK diagnostic entrypoints with a monotonic fake clock."""
import subprocess
import tempfile
import unittest
from pathlib import Path
from test_s3_product_regressions import ROOT, function, run_c

APP = ROOT / 'platforms/beken/runtime/src/xiaotai_app.c'

class AiTiming(unittest.TestCase):
    def test_session_and_round_timing(self):
        source = APP.read_text()
        self.assertIn('/* AI timing state:', source)
        state = source.split('/* AI timing state:', 1)[1].split('/* AI timing end */', 1)[0]
        state = state.split('*/', 1)[1]
        code = r'''
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <stdarg.h>
#include <cJSON.h>
static char logs[16000];
static void xiaotai_audio_log_playback_status(const char *stage){}
static uint32_t now;
static int s_runtime_mutex;
static bool locked;
static uint32_t rtos_get_time(void){return now;}
static void rtos_lock_mutex(int *p){assert(!locked);locked=true;}
static void rtos_unlock_mutex(int *p){assert(locked);locked=false;}
static void log_msg(const char *fmt,...){
 va_list ap;va_start(ap,fmt);size_t n=strlen(logs);
 vsnprintf(logs+n,sizeof(logs)-n,fmt,ap);va_end(ap);
}
#define BK_LOGI(tag,...) log_msg(__VA_ARGS__)
static int log_level=3;
#define BK_LOGD(tag,...) do {if(log_level>=4) log_msg(__VA_ARGS__);} while(0)
#define XIAOTAI_LOG_INFO 3
#define XIAOTAI_LOG_DEBUG 4
#define TAG "test"
#define xiaotai_log_write(level,tag,...) do {if(level<=log_level) log_msg(__VA_ARGS__);} while(0)
''' + state + r'''
static void event(uint32_t gen,uint32_t t,const char *json){
 now=t;ai_timing_command(gen,json,strlen(json));
 assert(!strstr(logs,"SECRET"));
}
int main(void){
 now=0;ai_timing_begin(1);
 now=178;ai_timing_stage(1,"token",0);
 now=10962;ai_timing_stage(1,"transport",0);
 now=11010;ai_timing_stage(1,"media",0);
 now=11310;ai_timing_stage(1,"start_sent",0);
 now=11655;ai_timing_stage(1,"active",0);
 assert(strstr(logs,"stage=active elapsed_ms=11655 step_ms=345"));
 assert(strstr(logs,"stage=transport elapsed_ms=10962 step_ms=10784"));
 size_t n=strlen(logs);ai_timing_stage(99,"active",0);assert(strlen(logs)==n);
 event(1,12000,"{\"method\":\"round_start\"}");
 event(1,12100,"{\"method\":\"round_start\"}"); /* duplicate */
 event(1,12200,"{\"method\":\"caption\",\"params\":{\"caption_type\":1,\"text\":\"SECRET hello\"}}");
 event(1,13000,"{\"method\":\"round_end\"}");
 assert(strstr(logs,"round=1 event=round_end round_ms=1000 asr_to_start_ms=-1 asr_to_caption_ms=-1 first_caption_ms=200"));
 n=strlen(logs);event(1,13001,"{\"method\":\"round_end\"}");assert(strlen(logs)==n);
 event(1,14000,"{\"method\":\"caption\",\"params\":{\"caption_type\":0,\"text\":\"SECRET question\",\"is_final\":true}}");
 event(1,14100,"{\"method\":\"caption\",\"params\":{\"caption_type\":0,\"text\":\"SECRET duplicate\",\"is_final\":true}}");
 event(1,14500,"{\"method\":\"round_start\"}");
 event(1,14600,"{\"method\":\"caption\",\"params\":{\"caption_type\":1,\"text\":\"SECRET reply\"}}");
 event(1,14700,"{\"method\":\"caption\",\"params\":{\"caption_type\":1,\"text\":\"SECRET next\"}}");
 event(1,16500,"{\"method\":\"round_end\"}");
 assert(strstr(logs,"round=2 event=round_end round_ms=2000 asr_to_start_ms=500 asr_to_caption_ms=600 first_caption_ms=100"));
 n=strlen(logs);event(99,17000,"{\"method\":\"round_start\"}");
 event(1,17000,"bad JSON");event(1,17000,"{\"method\":\"SECRET unknown\"}");assert(strlen(logs)==n);
 event(1,17000,"{\"method\":\"round_start\"}");
 now=17500;ai_timing_stage(1,"finish",-4);
 assert(strstr(logs,"round=3 event=incomplete round_ms=500"));
 n=strlen(logs);event(1,18000,"{\"method\":\"round_end\"}");ai_timing_stage(1,"finish",0);assert(strlen(logs)==n);
 now=UINT32_MAX-99;ai_timing_begin(2);
 now=100;ai_timing_stage(2,"active",0);
 assert(strstr(logs,"generation=2 stage=active elapsed_ms=200 step_ms=200"));
 event(2,200,"{\"method\":\"round_start\"}");
 event(2,300,"{\"method\":\"round_end\"}");
 assert(strstr(logs,"generation=2 round=1 event=round_end round_ms=100 asr_to_start_ms=-1"));
 assert(!strstr(logs,"event=round_start") && !strstr(logs,"event=first_caption") && !strstr(logs,"event=asr_final"));
 log_level=4;now=400;ai_timing_begin(3);
 event(3,500,"{\"method\":\"round_start\"}");
 assert(strstr(logs,"generation=3 round=1 event=round_start"));
 puts("AI timing replay PASS: connect=11655 ms, round=2000 ms, ASR->caption=600 ms; wrap/stale/duplicates/incomplete covered");
}
'''
        with tempfile.TemporaryDirectory() as tmp:
            path = Path(tmp)
            (path/'test.c').write_text(code)
            subprocess.run(['cc','-std=c11','-Wall','-Werror','-I/usr/include/cjson',str(path/'test.c'),'-lcjson','-o',str(path/'test')],check=True)
            subprocess.run([str(path/'test')],check=True)

    def test_start_session_has_no_fixed_delay(self):
        source=APP.read_text()
        run_c(r'''
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stddef.h>
#define XIAOTAI_OWNER_AI 1
#define SESSION_CONNECT_TIMEOUT_MS 15000
#define XIAOTAI_AI_COMMAND 0x2100
#define BK_FAIL -1
#define BK_LOGI(...) ((void)0)
#define BK_LOGE(...) ((void)0)
typedef struct {unsigned owner,generation;} xiaotai_runtime_t;
static xiaotai_runtime_t s_runtime={1,7};
static int s_runtime_mutex,prepared,sent,delay,disconnected,fail_prepare;
static bool current=true;
static char s_ai_request_id[32],s_device_id[32],s_ai_role_id[32];
static void rtos_lock_mutex(int *m){}
static void rtos_unlock_mutex(int *m){}
static unsigned rtos_get_time(void){return 100;}
static void rtos_delay_milliseconds(unsigned ms){delay+=ms;}
static bool xiaotai_runtime_connected(void *r,unsigned g){return current;}
static void arm_runtime_timeout_locked(unsigned g,unsigned ms){}
static void ai_timing_stage(unsigned g,const char *s,int e){}
static int xiaotai_tirtc_ai_prepare_media(void){prepared++;return fail_prepare?-1:0;}
static int xiaotai_tirtc_disconnect(void){disconnected++;return 0;}
static int xiaotai_ai_encode_start(char *buf,size_t n,const char *id,const char *dev,const char *role){return 1;}
static int xiaotai_tirtc_send_command(unsigned c,const char *buf,unsigned n){assert(prepared);sent++;return 0;}
''' + function(source,'handle_ai_connected') + r'''
int main(void){
 handle_ai_connected(7);assert(sent==1 && prepared==1 && delay==0);
 fail_prepare=1;handle_ai_connected(7);assert(sent==1 && disconnected==1);
 current=false;handle_ai_connected(7);assert(sent==1 && prepared==2 && disconnected==2);
 handle_ai_connected(6);assert(sent==1);
}
''')

    def test_runtime_wiring(self):
        source=APP.read_text()
        self.assertIn('ai_timing_begin(generation)', source)
        for name, stage in [('ai_token_response','token'),('on_ai_connected','transport'),('handle_ai_connected','media'),('on_ai_command','active'),('finish_ai','finish')]:
            self.assertIn('"'+stage+'"',function(source,name))
        self.assertIn('ai_timing_command(generation',function(source,'on_ai_command'))
        self.assertIn('"start_sent"',function(source,'handle_ai_connected'))

if __name__ == '__main__':
    unittest.main()
