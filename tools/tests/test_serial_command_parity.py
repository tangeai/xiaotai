"""Both platform registries and both handbooks must expose portable service commands."""
import re
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
ESP = ROOT / 'platforms/esp-idf/components/starter_console/src/starter_console.c'
BK = ROOT / 'platforms/beken/runtime/src/xiaotai_console.c'
PORTABLE = set('help status version restart wifi-change wifi-set wifi-clear tirtc-set tirtc-clear log-level ai-start ai-stop device-call device-answer wechat-call wechat-answer call-answer call-hangup call-status room-status room-sync room-join room-leave room-talk-start room-talk-stop ep-show ep-set ep-reset ntp-show ntp-set ntp-reset'.split())


class SerialCommandParity(unittest.TestCase):
    def test_platform_registries_and_manuals(self):
        esp = set(re.findall(r'register_command\(\s*"([a-z-]+)"', ESP.read_text())) | {'help'}
        bk = set(re.findall(r'\{"([a-z-]+)",', BK.read_text())) | {'help'}
        self.assertFalse(PORTABLE - esp, sorted(PORTABLE - esp))
        self.assertFalse(PORTABLE - bk, sorted(PORTABLE - bk))
        esp_probes = set('video-submit media-diag audio-trace audio-capture face-animation voice-config voice-diag voice-test'.split())
        self.assertEqual(esp - bk, esp_probes)
        self.assertEqual(bk - esp, set())
        for suffix in ('md', 'en.md'):
            text = (ROOT / f'docs/development/SERIAL_COMMANDS.{suffix}').read_text()
            commands = set(re.findall(r'`([a-z][a-z-]+)(?=[ `])', text))
            self.assertFalse((esp | bk) - commands, sorted((esp | bk) - commands))


    def test_esp_commands_submit_intents_and_propagate_failure(self):
        from test_s3_product_regressions import run_c, function
        source = ESP.read_text()
        handlers = ''.join(function(source, name) for name in (
            'request_result', 'command_dial', 'command_call_control', 'command_room_control',
            'command_wifi_change', 'command_log_level'))
        run_c(r'''
#include <assert.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
typedef int esp_err_t;
typedef int esp_log_level_t;
#define ESP_OK 0
#define ESP_ERR_INVALID_ARG 1
#define ESP_ERR_INVALID_STATE 2
#define STARTER_RUNTIME_WAITING 0
#define STARTER_CONTACT_WECHAT 1
#define STARTER_CONTACT_DEVICE 0
#define CONFIG_LOG_MAXIMUM_LEVEL 3
static int last, fail, source, pressed, app=3, sdk=4;
static const char *esp_err_to_name(int err){return "error";}
static const char *name_arg, *password_arg;
static struct {int state;} network;
#define starter_runtime_status_snapshot() network
static int wifi_manager_request_provisioning(void){last=1;return fail;}
static int starter_runtime_wechat_quick_call(void){last=2;return fail;}
static int starter_runtime_dial_named(int src,const char *name){last=3;source=src;name_arg=name;return fail;}
static int starter_runtime_call_accept(void){last=4;return fail;}
static int starter_runtime_call_hangup(void){last=5;return fail;}
static int starter_runtime_accept_from(int src){last=6;source=src;return fail;}
static int starter_runtime_room_join(const char *name,const char *pw){last=7;name_arg=name;password_arg=pw;return fail;}
static int starter_runtime_room_refresh(void){last=8;return fail;}
static int starter_runtime_room_leave(void){last=9;return fail;}
static int starter_runtime_room_ptt(bool down){last=10;pressed=down;return fail;}
static int esp_log_level_get(const char *tag){return app;}
static void esp_log_level_set(const char *tag,int level){app=level;}
static int starter_tirtc_log_level(void){return sdk;}
static void starter_tirtc_set_log_level(int level){sdk=level;}
''' + handlers + r'''
#define RUN(handler,...) handler(sizeof((char*[]){__VA_ARGS__})/sizeof(char*),(char*[]){__VA_ARGS__})
int main(void){
 assert(RUN(command_wifi_change,"wifi-change")==0 && last==1);
 network.state=1;last=0;assert(RUN(command_wifi_change,"wifi-change")==1 && !last);
 assert(RUN(command_dial,"wechat-call")==0 && last==2);
 assert(RUN(command_dial,"device-call","sister")==0 && last==3 && source==0 && !strcmp(name_arg,"sister"));
 assert(RUN(command_dial,"wechat-call","test")==0 && last==3 && source==1);
 last=0;assert(RUN(command_dial,"device-call")==1 && !last);
 assert(RUN(command_call_control,"call-answer")==0 && last==4);
 assert(RUN(command_call_control,"call-hangup")==0 && last==5);
 assert(RUN(command_call_control,"device-answer")==0 && last==6 && source==0);
 assert(RUN(command_call_control,"wechat-answer")==0 && last==6 && source==1);
 assert(RUN(command_room_control,"room-join","123456","0000")==0 && last==7 && !strcmp(password_arg,"0000"));
 assert(RUN(command_room_control,"room-join","123456")==0 && password_arg==NULL);
 assert(RUN(command_room_control,"room-sync")==0 && last==8);
 assert(RUN(command_room_control,"room-leave")==0 && last==9);
 assert(RUN(command_room_control,"room-talk-start")==0 && last==10 && pressed);
 assert(RUN(command_room_control,"room-talk-stop")==0 && !pressed);
 fail=1;assert(RUN(command_room_control,"room-sync")==1);
 assert(RUN(command_log_level,"log-level","app","4")==0 && app==4);
 assert(RUN(command_log_level,"log-level","tirtc","17")==0 && sdk==17);
 assert(RUN(command_log_level,"log-level","app","5")==1 && app==4);
 assert(RUN(command_log_level,"log-level","tirtc","3x")==1 && sdk==17);
 assert(RUN(command_log_level,"log-level","tirtc","-1")==1 && sdk==17);
}
''')


    def test_bk_preload_rejects_invalid_input_and_hides_secret(self):
        from test_s3_product_regressions import run_c, function
        source = BK.read_text()
        run_c(r'''
#include <assert.h>
#include <stdio.h>
#include <string.h>
typedef struct {char device_id[65],device_secret[257],client_id[65];} xiaotai_device_credentials_t;
static xiaotai_device_credentials_t stored;
static int writes, rebooted, fail;
static int xiaotai_storage_recover_device(const xiaotai_device_credentials_t *v){
 if(fail)return -1;
 stored=*v;writes++;return 0;
}
static void response(char *out,int cap,const char *msg){snprintf(out,cap,"%s",msg);}
static void complete_configuration_change(char *out,int cap,int rc,const char *yes,const char *no){
 if(!rc)rebooted++;
 response(out,cap,rc?no:yes);
}
''' + function(source,'command_tirtc_set') + r'''
int main(void){
 char out[200],long_id[66];memset(long_id,'x',65);long_id[65]=0;
 char *args[]={"tirtc-set","unit","private-test-value","client"};
 command_tirtc_set(out,sizeof out,2,args);assert(!writes && !rebooted);
 command_tirtc_set(out,sizeof out,4,args);
 assert(writes==1 && rebooted==1 && !strcmp(stored.client_id,"client"));
 assert(!strstr(out,"private-test-value"));
 command_tirtc_set(out,sizeof out,3,args);assert(writes==2 && !stored.client_id[0]);
 args[1]=long_id;command_tirtc_set(out,sizeof out,3,args);assert(writes==2);
 args[1]="";command_tirtc_set(out,sizeof out,3,args);assert(writes==2);
 args[1]="unit";fail=1;command_tirtc_set(out,sizeof out,3,args);
 assert(rebooted==2 && !strstr(out,"private-test-value"));
}
''')


if __name__ == '__main__':
    unittest.main()
