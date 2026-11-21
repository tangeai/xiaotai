"""Replay serial NTP configuration through production persistence and commands."""
import unittest
from test_s3_product_regressions import ROOT, function, run_c
from test_serial_ep_override import P4_CONFIG, BK_STORAGE, P4_CONSOLE, BK_CONSOLE

COMMON = ROOT / 'platforms/common/include'
PRELUDE = r'''
#include <assert.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>
#include "xiaotai_ntp_config.h"
'''

class SerialNtpServer(unittest.TestCase):
    def test_server_validation(self):
        run_c(PRELUDE + r'''
int main(void) {
 const char *good[]={"pool.ntp.org","time.lan","192.168.1.10","NTP-1.example.com","localhost"};
 const char *bad[]={"","http://ntp.org","ntp.org:123","bad host","-host","host-","a..b",".host","host.","a/b","a\n","256.1.1.1","1.2.3","1.2.3.4.5","010.0.0.1","08.0.0.1","1.00.0.1"};
 for(unsigned i=0;i<sizeof(good)/sizeof(good[0]);i++)assert(xiaotai_ntp_server_valid(good[i]));
 for(unsigned i=0;i<sizeof(bad)/sizeof(bad[0]);i++)assert(!xiaotai_ntp_server_valid(bad[i]));
 assert(!xiaotai_ntp_server_valid(NULL));
 char longname[255];memset(longname,'a',sizeof(longname));longname[254]=0;
 assert(!xiaotai_ntp_server_valid(longname));
 longname[64]=0;assert(!xiaotai_ntp_server_valid(longname));
}
''', [COMMON])

    def test_persistence_round_trip_failures_and_corruption(self):
        for bk in (False, True):
            with self.subTest(bk=bk):
                prefix = 'xiaotai_storage' if bk else 'runtime_config'
                source = (BK_STORAGE if bk else P4_CONFIG).read_text()
                funcs = '\n'.join(function(source, prefix + '_' + op + '_ntp_server')
                                  for op in ('load', 'save', 'clear'))
                fake = r'''
static char stored[254]; static int writes,fail;
#define BK_OK 0
#define BK_ERR_BUSY -9
#define BK_FAIL -1
#define BK_ERR_PARAM -2
#define XIAOTAI_NTP_SERVER_MAX 254
#define XIAOTAI_NTP_SERVER_KEY "xiaotai_ntp_v1"
static int storage_write_blob(const char *key,const void *v,size_t n){
 assert(!strcmp(key,"xiaotai_ntp_v1"));if(fail)return -1;memcpy(stored,v,n);writes++;return 0;
}
static int storage_delete(const char *key){if(fail)return -1;stored[0]=0;return 0;}
static size_t ef_get_env_blob(const char *key,void *out,size_t cap,size_t *saved){
 if(!stored[0])return 0;
 size_t n=strlen(stored)+1;*saved=n;if(n>cap)return 0;memcpy(out,stored,n);return n;
}
''' if bk else r'''
static char stored[254]; static int writes,fail;
#define ESP_OK 0
#define ESP_ERR_NVS_NOT_FOUND -1
#define ESP_ERR_INVALID_ARG -2
#define RUNTIME_NTP_SERVER_MAX 254
#define NTP_NVS_NAMESPACE "ntp_cfg"
#define NTP_NVS_KEY "server"
#define NVS_READONLY 1
#define NVS_READWRITE 2
typedef int esp_err_t;typedef int nvs_handle_t;
static int nvs_open(const char *ns,int mode,nvs_handle_t *h){assert(!strcmp(ns,"ntp_cfg"));if(fail)return -3;*h=1;return 0;}
static int nvs_get_str(int h,const char *k,char *v,size_t *n){
 assert(!strcmp(k,"server"));if(!stored[0])return -1;if(strlen(stored)+1>*n)return -2;strcpy(v,stored);return 0;
}
static int nvs_set_str(int h,const char *k,const char *v){assert(!strcmp(k,"server"));strcpy(stored,v);writes++;return 0;}
static int nvs_erase_key(int h,const char *k){stored[0]=0;return 0;}
static int nvs_commit(int h){return 0;}
static void nvs_close(int h){}
'''
                run_c(PRELUDE + fake + funcs + f'''
int main(void) {{
 char out[254];
 assert({prefix}_load_ntp_server(out,sizeof(out))!=0 && !out[0]);
 assert({prefix}_save_ntp_server("time.lan")==0 && writes==1);
 assert({prefix}_load_ntp_server(out,sizeof(out))==0 && !strcmp(out,"time.lan"));
 assert({prefix}_save_ntp_server("http://bad")!=0 && writes==1);
 assert({prefix}_load_ntp_server(out,2)!=0);
 fail=1;assert({prefix}_save_ntp_server("new.lan")!=0 && writes==1);
 assert({prefix}_clear_ntp_server()!=0);fail=0;
 strcpy(stored,"bad host");assert({prefix}_load_ntp_server(out,sizeof(out))!=0 && !out[0]);
 assert({prefix}_clear_ntp_server()==0);
 assert({prefix}_load_ntp_server(out,sizeof(out))!=0 && !out[0]);
}}
''', [COMMON])

    def test_commands_only_reboot_after_successful_persistence(self):
        for bk in (False, True):
            source = (BK_CONSOLE if bk else P4_CONSOLE).read_text()
            prefix = 'xiaotai_storage' if bk else 'runtime_config'
            for op in ('show','set','reset'):
                self.assertIn('"ntp-'+op+'"', source)
            funcs='\n'.join(function(source,'command_ntp_'+op) for op in ('show','set','reset'))
            if bk: funcs=function(source,'complete_configuration_change')+funcs
            fake=r'''
#define RUNTIME_NTP_SERVER_MAX 254
#define XIAOTAI_NTP_SERVER_MAX 254
#define BK_OK 0
#define BK_ERR_BUSY -9
#define ESP_OK 0
#define ESP_ERR_NVS_NOT_FOUND -1
typedef int esp_err_t;
static int fail,saves,clears,restarts;
static int load(char *p,size_t n){strcpy(p,"time.lan");return 0;}
static int save(const char *p){saves++;return fail;}
static int clear(void){clears++;return fail;}
#define runtime_config_load_ntp_server load
#define runtime_config_save_ntp_server save
#define runtime_config_clear_ntp_server clear
#define xiaotai_storage_load_ntp_server load
#define xiaotai_storage_save_ntp_server save
#define xiaotai_storage_clear_ntp_server clear
static int schedule_reboot(void){restarts++;return 0;}
static void esp_restart(void){restarts++;}
static void vTaskDelay(int n){}
#define pdMS_TO_TICKS(n) (n)
static const char *esp_err_to_name(int rc){return "error";}
static void response(char *p,int n,const char *s){snprintf(p,n,"%s",s);}
'''
            arg='out,sizeof(out),' if bk else ''
            run_c(PRELUDE + fake + funcs + f'''
int main(void) {{
 {'char out[512];' if bk else ''}
 char *args[]={{"ntp-set","time.lan"}};
 command_ntp_show({arg}1,args);assert(!restarts);
 command_ntp_set({arg}1,args);assert(!saves && !restarts);
 fail=-1;command_ntp_set({arg}2,args);assert(saves==1 && !restarts);
 fail=0;command_ntp_set({arg}2,args);assert(saves==2 && restarts==1);
 command_ntp_reset({arg}2,args);assert(!clears && restarts==1);
 fail=-1;command_ntp_reset({arg}1,args);assert(clears==1 && restarts==1);
 fail=0;command_ntp_reset({arg}1,args);assert(clears==2 && restarts==2);
}}
''', [COMMON])

    def test_every_time_sync_path_loads_persisted_override(self):
        for path,loader in (
            ('platforms/esp-idf/components/platform_client/src/platform_client.c','runtime_config'),
            ('platforms/beken/runtime/src/xiaotai_platform_client.c','xiaotai_storage'),
        ):
            self.assertIn(loader+'_load_ntp_server(', (ROOT/path).read_text())

    def test_esp_primary_is_used_and_has_background_lifetime(self):
        source=(ROOT/'platforms/esp-idf/components/platform_client/src/platform_client.c').read_text()
        run_c(PRELUDE+r'''
#include <time.h>
typedef int esp_err_t;
typedef struct {const char *servers[2];} esp_sntp_config_t;
#define ESP_OK 0
#define ESP_ERR_INVALID_STATE 1
#define ESP_ERR_TIMEOUT 2
#define ESP_SNTP_SERVER_LIST(a,b) {a,b}
#define ESP_NETIF_SNTP_DEFAULT_CONFIG_MULTIPLE(n,list) ((esp_sntp_config_t){.servers=list})
#define ESP_LOGI(...) ((void)0)
#define ESP_LOGE(...) ((void)0)
#define pdMS_TO_TICKS(x) (x)
static bool s_sntp_initialized;
static char s_ntp_server[254];
static int loads,missing;
static const char *retained;
static int runtime_config_load_ntp_server(char *out,size_t cap){
 loads++;if(missing)return -1;
 snprintf(out,cap,"time.lan");return 0;
}
static int esp_netif_sntp_init(const esp_sntp_config_t *cfg){
 assert(!strcmp(cfg->servers[0],missing?"pool.ntp.org":"time.lan"));
 assert(!strcmp(cfg->servers[1],"ntp.aliyun.com"));retained=cfg->servers[0];return 0;
}
static int esp_netif_sntp_sync_wait(unsigned ms){return 0;}
''' + function(source,'sync_clock') + r'''
int main(void){
 assert(sync_clock()==0 && loads==1);
 assert(retained==s_ntp_server && !strcmp(retained,"time.lan"));
 assert(sync_clock()==0 && loads==1);
 s_sntp_initialized=false;missing=1;
 assert(sync_clock()==0 && loads==2 && !strcmp(retained,"pool.ntp.org"));
}
''', [COMMON])

    def test_bk_query_does_not_disguise_unreadable_override(self):
        source=BK_CONSOLE.read_text()
        run_c(PRELUDE+r'''
#define BK_OK 0
#define BK_ERR_BUSY -9
#define XIAOTAI_NTP_SERVER_MAX 254
static void response(char*out,int n,const char*s){snprintf(out,n,"%s",s);}
static int xiaotai_storage_load_ntp_server(char*out,size_t n){return -1;}
'''+function(source,'command_ntp_show')+r'''
int main(void){
 char output[160];char *args[]={"ntp-show"};command_ntp_show(output,sizeof(output),1,args);
 assert(strstr(output,"no valid saved override") && strstr(output,"unreadable") && strstr(output,"corrupt"));
}
''',[COMMON])

    def test_bk_attempts_custom_primary_then_original_fallback(self):
        source=(ROOT/'platforms/beken/runtime/src/xiaotai_platform_client.c').read_text()
        run_c('#define _POSIX_C_SOURCE 200112L\n'+PRELUDE+r'''
#include <stdint.h>
#include <time.h>
#include <sys/time.h>
#include <sys/socket.h>
#include <netdb.h>
#include "xiaotai_ntp_response.h"
static int bk_fill_rand(void *out,size_t n){memset(out,1,n);return 0;}
#include <unistd.h>
#define BK_OK 0
#define BK_ERR_BUSY -9
#define BK_FAIL -1
#define XIAOTAI_NTP_SERVER_MAX 254
#define NTP_UNIX_EPOCH_DELTA 2208988800UL
#define BK_LOGE(...) ((void)0)
#define BK_LOGI(...) ((void)0)
static bool s_clock_synchronized;
static int missing,attempts;
static time_t xiaotai_time_now(void){return 0;}
static int bk_rtc_settimeofday(const struct timeval *tv,void *tz){return 0;}
static int xiaotai_storage_load_ntp_server(char *out,size_t cap){
 if(missing)return -1;
 snprintf(out,cap,"time.lan");return 0;
}
static int resolve(const char *name,const char *port,const struct addrinfo *hint,struct addrinfo **out){
 assert(!strcmp(port,"123") && hint->ai_family==AF_INET);
 assert(!strcmp(name,attempts++%2 ? "ntp.aliyun.com" : missing?"pool.ntp.org":"time.lan"));
 return -1;
}
#define getaddrinfo resolve
''' + function(source,'synchronize_clock') + r'''
int main(void){
 assert(synchronize_clock()==BK_FAIL && attempts==2);
 missing=1;assert(synchronize_clock()==BK_FAIL && attempts==4);
 s_clock_synchronized=true;assert(synchronize_clock()==BK_OK && attempts==4);
}
''', [COMMON], cflags=['-Wno-unused-variable'])

if __name__ == '__main__':
    unittest.main()
