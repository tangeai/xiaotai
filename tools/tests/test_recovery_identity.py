from config_store_fixture import config_store_fixture
"""Replay manual recovery versus delayed background credential persistence."""
import unittest
from test_s3_product_regressions import ROOT, function, run_c

COMMON = ROOT/'platforms/common/include'

class RecoveryIdentity(unittest.TestCase):
    def test_platform_storage_recovery_excludes_background_writers(self):
        for bk in (False, True):
            with self.subTest(bk=bk):
                p=ROOT/('platforms/beken/runtime/src/xiaotai_storage.c' if bk else
                        'platforms/esp-idf/components/runtime_config/src/runtime_config.c')
                src=p.read_text()
                save='xiaotai_storage_save_device' if bk else 'runtime_config_save_tirtc'
                clear='xiaotai_storage_clear_device' if bk else 'runtime_config_clear_tirtc'
                recovery='xiaotai_storage_recover_device' if bk else 'runtime_config_recover_tirtc'
                typ='xiaotai_device_credentials_t' if bk else 'runtime_tirtc_config_t'
                rawsave='save_device_unlocked' if bk else 'save_tirtc_unlocked'
                rawclear='clear_device_unlocked' if bk else 'clear_tirtc_unlocked'
                run_c(config_store_fixture() + r'''
#include <assert.h>
#include <stdbool.h>
#include <string.h>
#include "xiaotai_identity_gate.h"
#define BK_OK 0
#define BK_FAIL -1
#define BK_ERR_BUSY -2
#define ESP_OK 0
#define ESP_ERR_INVALID_STATE -2
typedef int esp_err_t;
typedef struct {int identity;} runtime_tirtc_config_t;
typedef runtime_tirtc_config_t xiaotai_device_credentials_t;
static xiaotai_identity_gate_t s_identity_gate;
static int stored,fail,overlap,writes;
''' + f'static int {rawsave}(const {typ} *v);\nstatic int {rawclear}(void);\n' +
                      function(src,save)+function(src,clear)+function(src,recovery)+f'''
static int {rawsave}(const {typ} *v){{
 if(overlap){{overlap=0;assert({recovery}(NULL)!=0);}}
 if(fail)return -1;
 stored=v->identity;writes++;return 0;
}}
static int {rawclear}(void){{if(fail)return -1;stored=0;writes++;return 0;}}
int main(void){{
 {typ} background={{.identity=1}},manual={{.identity=2}};
 overlap=1;assert({save}(&background)==0 && stored==1);
 fail=1;assert({recovery}(NULL)!=0);fail=0;
 assert({save}(&background)==0);
 assert({recovery}(NULL)==0 && stored==0);
 int before=writes;
 assert({save}(&background)!=0 && stored==0 && writes==before);
 assert({clear}()!=0 && writes==before);
 /* Reboot scheduling failure may leave the console running: retry must work. */
 assert({recovery}(&manual)==0 && stored==2);
 fail=1;assert({recovery}(NULL)!=0);fail=0;
 assert({save}(&background)!=0 && stored==2);
 assert({recovery}(NULL)==0 && stored==0);
}}
''',[COMMON])

    def test_bk_command_retains_recovery_when_reboot_fails(self):
        storage=(ROOT/'platforms/beken/runtime/src/xiaotai_storage.c').read_text()
        console=(ROOT/'platforms/beken/runtime/src/xiaotai_console.c').read_text()
        helper=function(console,'complete_configuration_change') if 'static void complete_configuration_change' in console else ''
        run_c(config_store_fixture() + r'''
#include <assert.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include "xiaotai_identity_gate.h"
#define BK_OK 0
#define BK_ERR_BUSY -2
#define BK_ERR_PARAM -3
typedef struct {int id;} xiaotai_device_credentials_t;
static xiaotai_identity_gate_t s_identity_gate;
static int stored=1,reboot_error=1,reboot_calls;
static int save_device_unlocked(const xiaotai_device_credentials_t *v){stored=v->id;return 0;}
static int clear_device_unlocked(void){stored=0;return 0;}
static void response(char *out,int n,const char*s){snprintf(out,n,"%s",s);}
static int schedule_reboot(void){reboot_calls++;return reboot_error;}
'''+function(storage,'xiaotai_storage_save_device')+function(storage,'xiaotai_storage_recover_device')+
              helper+function(console,'command_tirtc_clear')+r'''
int main(void){
 char out[160];char *args[]={"tirtc-clear"};
 command_tirtc_clear(out,sizeof(out),1,args);
 assert(stored==0 && reboot_calls==1 && strstr(out,"updated") && strstr(out,"restart scheduling failed"));
 xiaotai_device_credentials_t late={.id=9};assert(xiaotai_storage_save_device(&late)!=0 && stored==0);
 reboot_error=0;command_tirtc_clear(out,sizeof(out),1,args);
 assert(reboot_calls==2 && strstr(out,"restarting"));
}
''',[COMMON])

    def test_serial_identity_commands_use_recovery_transaction(self):
        esp=(ROOT/'platforms/esp-idf/components/starter_console/src/starter_console.c').read_text()
        bk=(ROOT/'platforms/beken/runtime/src/xiaotai_console.c').read_text()
        self.assertIn('runtime_config_recover_tirtc(&config)',function(esp,'command_tirtc_set'))
        self.assertIn('runtime_config_recover_tirtc(NULL)',function(esp,'command_tirtc_clear'))
        self.assertIn('xiaotai_storage_recover_device(NULL)',function(bk,'command_tirtc_clear'))

if __name__=='__main__':unittest.main()
