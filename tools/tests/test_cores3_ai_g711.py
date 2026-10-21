"""CoreS3 experiment: G.711 A-law uplink, unchanged Opus downlink."""
import subprocess
import tempfile
import unittest
from pathlib import Path
from test_s3_product_regressions import ROOT, function, run_c
import test_s3_opus_media as opus
import test_s3_voip_audio as routing

class CoreS3AiG711(unittest.TestCase):
    def test_sdk_ai_uplink_codec_and_flags(self):
        source=routing.TIRTC.read_text()
        for enabled in (0,1):
            code=routing.S3VoipAudio().code().replace(
                'assert(c==(void *)1 && data && f->stream_id==expected && f->ts==20 && f->length==160);',
                'assert(c==(void *)1 && data && f->stream_id==expected && f->ts==20 && f->length==160); assert(f->media==TIRTC_AUDIO_ALAW && f->flags==TIRTC_AUDIOSAMPLE_8K16B1C);')
            run_c(f'#define CONFIG_STARTER_AI_UPLINK_G711A {enabled}\n'+code+
                  function(source,'starter_tirtc_send_alaw')+r'''
int main(void){
 current_mode=STARTER_TIRTC_AI;expected=1;
 assert(starter_tirtc_send_alaw(20,"x",160)==(CONFIG_STARTER_AI_UPLINK_G711A ? 0 : -1));
 current_mode=STARTER_TIRTC_H5;expected=10;assert(starter_tirtc_send_alaw(20,"x",160)==0);
 current_mode=STARTER_TIRTC_VOIP;expected=0;assert(starter_tirtc_send_alaw(20,"x",160)==0);
 s_connection=0;assert(starter_tirtc_send_alaw(20,"x",160)==-1);
}
''')

    def test_g711_uplink_uses_8k_frames(self):
        opus.S3OpusMedia().check_uplink(g711=True)

    def test_g711_preroll_and_live_join(self):
        opus.S3OpusMedia().check_preroll(g711=True)

    def test_opus_downlink_still_16k(self):
        opus.S3OpusMedia().check_downlink(g711=True)

    def test_start_request_and_negotiation_are_directional(self):
        source=(ROOT/'platforms/esp-idf/components/starter_runtime/src/starter_runtime.c').read_text()
        for enabled in (0,1):
            code=f'#define CONFIG_STARTER_AI_UPLINK_G711A {enabled}\n'+r'''
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <cjson/cJSON.h>
#define ESP_ERR_INVALID_STATE -1
#define ESP_ERR_NO_MEM -2
#define ESP_LOGE(...) ((void)0)
#define ESP_LOGI(...) ((void)0)
#define AI_COMMAND 0x2000
#define AI_RESPONSE_TIMEOUT_MS 10000
static int64_t s_ai_start_at_ms;
static uint32_t s_connection_generation=1;
static char s_ai_request_id[32],s_device_id[]="test-device",s_ai_role_id[]="test-role";
static char request[1024];
static bool starter_tirtc_connected(void){return true;}
static void finish_session(int error){assert(0);}
static unsigned esp_random(void){return 42;}
static void arm_session_timeout(int ms){assert(ms==10000);}
static int starter_tirtc_send_command(unsigned cmd,const void *data,unsigned len){
 assert(cmd==AI_COMMAND && len<sizeof request);memcpy(request,data,len);request[len]=0;return 0;
}
''' + function(source,'send_ai_start')+function(source,'ai_audio_profile_valid')+r'''
int main(void){
 send_ai_start();cJSON *root=cJSON_Parse(request);assert(root);
 cJSON *params=cJSON_GetObjectItemCaseSensitive(root,"params");
 cJSON *input=cJSON_GetObjectItemCaseSensitive(params,"input_audio");
 cJSON *output=cJSON_GetObjectItemCaseSensitive(params,"output_audio");
 const char *expected=CONFIG_STARTER_AI_UPLINK_G711A ? "alaw" : "opus";
 assert(!strcmp(cJSON_GetObjectItemCaseSensitive(input,"codec")->valuestring,expected));
 assert(cJSON_GetObjectItemCaseSensitive(input,"sample_rate")->valueint==(CONFIG_STARTER_AI_UPLINK_G711A?8000:16000));
 assert(!strcmp(cJSON_GetObjectItemCaseSensitive(output,"codec")->valuestring,"opus"));
 assert(cJSON_GetObjectItemCaseSensitive(output,"sample_rate")->valueint==16000);
 assert(ai_audio_profile_valid(input,true));assert(ai_audio_profile_valid(output,false));
 if(CONFIG_STARTER_AI_UPLINK_G711A){
  assert(!ai_audio_profile_valid(output,true));assert(!ai_audio_profile_valid(input,false));
  cJSON_ReplaceItemInObject(input,"codec",cJSON_CreateString("g711a"));
  assert(ai_audio_profile_valid(input,true));
  cJSON_ReplaceItemInObject(input,"codec",cJSON_CreateString("pcm"));
  assert(!ai_audio_profile_valid(input,true));
  cJSON_ReplaceItemInObject(input,"codec",cJSON_CreateString("alaw"));
  cJSON_SetNumberValue(cJSON_GetObjectItemCaseSensitive(input,"sample_rate"),16000);
  assert(!ai_audio_profile_valid(input,true));
  cJSON_SetNumberValue(cJSON_GetObjectItemCaseSensitive(input,"sample_rate"),8000.5);
  assert(!ai_audio_profile_valid(input,true));
  cJSON_SetNumberValue(cJSON_GetObjectItemCaseSensitive(input,"sample_rate"),8000);
  cJSON_SetNumberValue(cJSON_GetObjectItemCaseSensitive(input,"channels"),1.5);
  assert(!ai_audio_profile_valid(input,true));
  cJSON_SetNumberValue(cJSON_GetObjectItemCaseSensitive(input,"channels"),1);
 }
 cJSON_SetNumberValue(cJSON_GetObjectItemCaseSensitive(input,"channels"),2);
 assert(!ai_audio_profile_valid(input,true));assert(!ai_audio_profile_valid(NULL,true));
 cJSON_Delete(root);
}
'''
            with tempfile.TemporaryDirectory(prefix='cores3-g711-') as tmp:
                p=Path(tmp);(p/'test.c').write_text(code)
                subprocess.run(['cc','-std=c11','-Wall','-Werror',str(p/'test.c'),'-lcjson','-o',str(p/'test')],check=True)
                subprocess.run([str(p/'test')],check=True)

    def test_start_response_admission_and_safe_failure_detail(self):
        source=(ROOT/'platforms/esp-idf/components/starter_runtime/src/starter_runtime.c').read_text()
        # Replay the production command handler through the complete CONNECTING
        # branches. ACTIVE caption/tool handling is outside this negotiation test.
        handler=function(source,'handle_ai_command').split(
            ' else if (state == STARTER_RUNTIME_AI_ACTIVE)')[0]+' else { cJSON_Delete(root); }\n}'
        code=r'''
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <stdarg.h>
#include <cjson/cJSON.h>
#define CONFIG_STARTER_AI_UPLINK_G711A 1
#define STARTER_TIRTC_AI 2
#define AI_COMMAND 0x2100
#define STARTER_RUNTIME_AI_CONNECTING 2
#define ESP_FAIL -1
#define ESP_ERR_NOT_SUPPORTED 262
#define TAG "test"
typedef int starter_runtime_state_t;
typedef struct { unsigned mode,command,generation; const char *text; size_t length; } runtime_event_t;
static unsigned s_connection_generation=7;
static char s_ai_request_id[]="request-secret";
static bool s_ai_media_pending;
static struct { int emotion; } s_ai_view;
static int finished,admitted;
static char logs[2048];
static void log_msg(const char *fmt,...){
 va_list args;va_start(args,fmt);
 size_t n=strlen(logs);vsnprintf(logs+n,sizeof(logs)-n,fmt,args);va_end(args);
}
#define ESP_LOGE(tag,...) log_msg(__VA_ARGS__)
#define ESP_LOGI(tag,...) log_msg(__VA_ARGS__)
static void diagnostic_event(const char *label,int value){}
static starter_runtime_state_t session_state(void){return STARTER_RUNTIME_AI_CONNECTING;}
static void finish_session(int err){finished=err;}
static void xiaotai_ai_view_init(void *view){}
static void product_set_emotion(int emotion,bool present){}
static bool starter_media_ai_input_ready(void){return true;}
static void service_ai_media_admission(void){assert(s_ai_media_pending);admitted++;}
'''+function(source,'ai_audio_profile_valid')+function(source,'ai_log_start_response')+handler+r'''
static void receive(const char *id,const char *session,const char *codec,int rate,int channels,const char *output){
 char json[1024];snprintf(json,sizeof(json),
 "{\"id\":\"%s\",\"result\":{\"session_id\":\"%s\","
 "\"input_audio\":{\"codec\":\"%s\",\"sample_rate\":%d,\"channels\":%d},"
 "\"output_audio\":{\"codec\":\"%s\",\"sample_rate\":16000,\"channels\":1},"
 "\"token\":\"never-log-token\"}}",id,session,codec,rate,channels,output);
 runtime_event_t event={2,AI_COMMAND,7,json,strlen(json)};
 finished=admitted=0;s_ai_media_pending=false;logs[0]=0;
 handle_ai_command(&event);
 assert(!strstr(logs,"request-secret") && !strstr(logs,"session-secret") && !strstr(logs,"never-log"));
}
int main(void){
 receive("request-secret","session-secret","alaw",8000,1,"opus");
 assert(admitted==1 && finished==0);
 receive("request-secret","session-secret","g711a",8000,1,"opus");
 assert(admitted==1 && finished==0);
 receive("request-secret","session-secret","alaw",16000,1,"opus");
 assert(!admitted && finished==262);
 assert(strstr(logs,"input codec=alaw rate=16000 channels=1 supported=0"));
 assert(strstr(logs,"output codec=opus rate=16000 channels=1 supported=1"));
 receive("request-secret","session-secret","alaw",8000,2,"opus");
 assert(!admitted && finished==262 && strstr(logs,"channels=2 supported=0"));
 receive("request-secret","session-secret","alaw",8000,1,"alaw");
 assert(!admitted && finished==262 && strstr(logs,"output codec=alaw"));
 receive("request-secret","session-secret","never-log-codec",8000,1,"opus");
 assert(!admitted && finished==262 && strstr(logs,"input codec=unknown"));
 receive("request-secret","","alaw",8000,1,"opus");
 assert(!admitted && finished==262 && strstr(logs,"session-id-present=0"));
 receive("stale-request","session-secret","alaw",8000,1,"opus");
 assert(!admitted && finished==0 && logs[0]==0);
 const char *missing="{\"id\":\"request-secret\",\"result\":{\"session_id\":\"session-secret\"}}";
 runtime_event_t event={2,AI_COMMAND,7,missing,strlen(missing)};
 handle_ai_command(&event);
 assert(!admitted && finished==262 && strstr(logs,"input codec=missing rate=-1 channels=-1 supported=0"));
}
'''
        with tempfile.TemporaryDirectory(prefix='cores3-ai-response-') as tmp:
            p=Path(tmp);(p/'test.c').write_text(code)
            subprocess.run(['cc','-std=c11','-Wall','-Werror',str(p/'test.c'),'-lcjson','-o',str(p/'test')],check=True)
            subprocess.run([str(p/'test')],check=True)

if __name__=='__main__': unittest.main()
