import unittest
from pathlib import Path
import sys
sys.path.insert(0,'tools/tests')
from test_s3_product_regressions import function,run_c
s=Path('platforms/beken/runtime/src/xiaotai_storage.c').read_text()
c=Path('platforms/beken/runtime/src/xiaotai_console.c').read_text()

class WifiRecovery(unittest.TestCase):
    def test_manual_recovery_excludes_delayed_portal(self):
        run_c(r'''
        #include <assert.h>
        #include "xiaotai_identity_gate.h"
        static xiaotai_identity_gate_t s_wifi_gate;
        #include <stdbool.h>
        #include <stdint.h>
        #include <stdatomic.h>
        #include <stdio.h>
        #include <string.h>
        #define BK_OK 0
        #define BK_FAIL -1
        #define BK_ERR_BUSY -2
        #define BK_ERR_PARAM -3
        #define EF_NO_ERR 0
        #define EF_ENV_NAME_ERR -4
        #define XIAOTAI_WIFI_KEY "wifi"
        #define XIAOTAI_WIFI_SSID_MAX 32
        #define XIAOTAI_WIFI_PASSWORD_MAX 64
        typedef int EfErrCode;
        typedef struct {char ssid[33],password[65];} xiaotai_wifi_credentials_t;
        typedef struct {int version;} xiaotai_record_header_t;
        typedef struct {xiaotai_record_header_t header;xiaotai_wifi_credentials_t payload;} xiaotai_wifi_record_t;
        static atomic_bool s_storage_reset_pending;
        static atomic_uint s_storage_writers;
        static bool exists;static xiaotai_wifi_record_t record;
        static int ef_set_env_blob(const char*k,const void*v,size_t n){assert(n==sizeof(record));memcpy(&record,v,n);exists=true;return 0;}
        static int ef_del_env(const char*k){exists=false;return 0;}
        static void make_header(xiaotai_record_header_t*h,const void*p,size_t n){h->version=1;}
        static void response(char*out,int capacity,const char*s){snprintf(out,capacity,"%s",s);}
        static int schedule_reboot(void){return 0;}
        '''+''.join(function(s,n) for n in ['storage_write_begin','storage_write_blob','storage_delete','terminated','xiaotai_wifi_credentials_valid','save_wifi_unlocked','clear_wifi_unlocked','xiaotai_storage_save_wifi','xiaotai_storage_clear_wifi','xiaotai_storage_recover_wifi'])+''.join(function(c,n) for n in ['complete_configuration_change','command_wifi_clear','command_wifi_set'])+r'''
        int main(void){
         char out[160];char *clear[]={"wifi-clear"};
         command_wifi_clear(out,sizeof(out),1,clear);
         assert(!exists && strstr(out,"cleared"));
         xiaotai_wifi_credentials_t portal={.ssid="portal-ssid",.password="portal-password"};
         assert(xiaotai_storage_save_wifi(&portal)==BK_ERR_BUSY);
         char *set[]={"wifi-set","serial-ssid","serial-password"};
         command_wifi_set(out,sizeof(out),3,set);
         assert(!strcmp(record.payload.ssid,"serial-ssid"));
         assert(xiaotai_storage_save_wifi(&portal)==BK_ERR_BUSY);
        }
        ''',[Path('platforms/common/include')])

if __name__=="__main__":unittest.main()
