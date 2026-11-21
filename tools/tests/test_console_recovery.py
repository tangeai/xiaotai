"""Console recovery remains available before network and application readiness."""
import unittest
from test_s3_product_regressions import ROOT, function, run_c

ESP=ROOT/'platforms/esp-idf/components/starter_console/src/starter_console.c'
BK=ROOT/'platforms/beken/runtime/src/xiaotai_console.c'
MAIN=ROOT/'platforms/esp-idf/main/app_main.c'
COMMON=ROOT/'platforms/common/include'

class ConsoleRecovery(unittest.TestCase):
    def test_esp_boot_starts_console_before_optional_initialization(self):
        source=MAIN.read_text()
        boot=function(source,'app_main')
        self.assertLess(boot.index('starter_console_start('),boot.index('starter_media_init('))
        self.assertNotIn('ESP_ERROR_CHECK(',boot)
        self.assertNotIn('ESP_ERROR_CHECK(',function(source,'init_nvs'))
        worker=function(source,'starter_start_task')
        self.assertLess(worker.index('starter_console_set_runtime_ready(true)'),
                        worker.index('platform_client_start(&platform)'))

    def test_bk_console_precedes_media_and_ui(self):
        for board in ('lckfb','mgkfb'):
            s=(ROOT/f'firmware/beken/bk7258/{board}-bk7258-xiaotai/ap/ap_main.c').read_text()
            self.assertLess(s.index('xiaotai_console_start()'),s.index('media_service_init()'))
            self.assertLess(s.index('xiaotai_console_start()'),s.index('xiaotai_ui_init()'))

    def test_esp_dispatch_blocks_uninitialized_business_and_allows_recovery(self):
        s=ESP.read_text()
        run_c(r'''
#include <assert.h>
#include <stdbool.h>
#include <stdatomic.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>
#include "xiaotai_console_policy.h"
typedef int (*esp_console_cmd_func_t)(int,char**);
static atomic_bool s_runtime_ready;
static unsigned calls;
static int handler(int argc,char **argv){calls++;return 17;}
typedef struct {const char *name;esp_console_cmd_func_t function;} console_entry_t;
static console_entry_t s_command_entries[]={
 {"ntp-set",handler},{"ep-reset",handler},{"wifi-set",handler},
 {"tirtc-clear",handler},{"restart",handler},{"status",handler},
 {"ai-start",handler},{"voice-diag",handler},{"task-diag",handler}};
static size_t s_command_count=sizeof(s_command_entries)/sizeof(s_command_entries[0]);
''' + function(s,'starter_console_set_runtime_ready') + function(s,'dispatch_command') + r'''
int main(void){
 char *args[]={NULL,"server"};
 for(unsigned scenario=0;scenario<6;scenario++){
  starter_console_set_runtime_ready(false);
  for(size_t i=0;i<s_command_count;i++){
   args[0]=(char*)s_command_entries[i].name;unsigned before=calls;
   int result=dispatch_command(2,args);
   if(i<5)assert(result==17 && calls==before+1);
   else assert(calls==before);
  }
 }
 starter_console_set_runtime_ready(true);
 args[0]="ai-start";assert(dispatch_command(1,args)==17);
 starter_console_set_runtime_ready(false);
 assert(dispatch_command(1,args)!=17);
 assert(dispatch_command(0,NULL)!=0);
}
''',[COMMON])

    def test_esp_initialization_failures_retain_console(self):
        source=MAIN.read_text()
        run_c(r'''
#define _POSIX_C_SOURCE 200809L
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <inttypes.h>
#include <stdlib.h>
#include <time.h>
#include <stddef.h>
typedef int esp_err_t;
#define ESP_OK 0
#define ESP_ERR_NVS_NO_FREE_PAGES 10
#define ESP_ERR_NVS_NEW_VERSION_FOUND 11
#define TAG "test"
#define MALLOC_CAP_INTERNAL 1
#define MALLOC_CAP_SPIRAM 2
#define MALLOC_CAP_8BIT 4
#define STARTER_TASK_STACK_BYTES 4096
#define TIRTC_INTERNAL_RESERVE_BYTES 4096
#define pdPASS 1
static int failure,console_calls,stages,workers;
static void *s_tirtc_internal_reserve;
static void log_emit(const char *tag,const char *format,...){(void)tag;(void)format;}
#define ESP_LOGE log_emit
#define ESP_LOGW log_emit
#define ESP_LOGI log_emit
static void allocation_failed(void){}
static int heap_caps_register_failed_alloc_callback(void (*f)(void)){return 0;}
static int nvs_flash_init(void){return failure==1 ? 1 : 0;}
static int nvs_flash_erase(void){return 0;}
static const char *esp_err_to_name(int e){return "error";}
static int starter_console_start(void){console_calls++;return 0;}
typedef struct {const char *project_name,*version,*date,*time;} esp_app_desc_t;
static esp_app_desc_t app={"test","test","test","test"};
static const esp_app_desc_t *esp_app_get_description(void){return &app;}
static int esp_app_get_elf_sha256(char *s,size_t n){s[0]=0;return 0;}
static const char *esp_get_idf_version(void){return "test";}
static int esp_reset_reason(void){return 0;}
typedef struct {unsigned cores,revision;} esp_chip_info_t;
static void esp_chip_info(esp_chip_info_t *c){c->cores=1;c->revision=0;}
static uint32_t heap_caps_get_total_size(int caps){return 100000;}
static const char *starter_tirtc_version(void){return "test";}
static const char *starter_tirtc_build_info(void){return "test";}
static int starter_media_init(void){assert(console_calls==1);stages|=1;return failure==2;}
static int starter_product_start(void){stages|=2;return failure==3;}
static int wifi_manager_start(void){stages|=4;return failure==4;}
static void log_heap_snapshot(const char *s){}
static void starter_start_task(void *p){}
static int xTaskCreate(void (*f)(void*),const char *n,int size,void *p,int pri,void *h){workers++;return failure==5?0:1;}
static void *heap_caps_malloc(size_t n,int caps){return NULL;}
'''+function(source,'init_nvs')+function(source,'app_main')+r'''
int main(void){
 for(failure=0;failure<=5;failure++){
  console_calls=stages=workers=0;app_main();
  assert(console_calls==1);
  if(failure==1)assert(stages==0 && workers==0);
  if(failure==2)assert(stages==1 && workers==0);
  if(failure==3)assert(stages==3 && workers==0);
  if(failure==4)assert(stages==7 && workers==0);
  if(failure==0 || failure==5)assert(stages==7 && workers==1);
 }
}
''')

    def test_wifi_netif_failures_return_and_cleanup(self):
        source=(ROOT/'platforms/esp-idf/components/wifi_manager/src/wifi_manager.c').read_text()
        run_c(r'''
#include <assert.h>
#include <stdbool.h>
#include <stddef.h>
typedef int esp_err_t;
typedef int esp_netif_t;
typedef int esp_netif_config_t;
#define ESP_OK 0
#define ESP_ERR_NO_MEM 99
#define ESP_NETIF_DEFAULT_WIFI_STA() 0
#define ESP_NETIF_DEFAULT_WIFI_AP() 1
static int failure,allocated,attached,handlers,destroyed,ap_expected;
static esp_netif_t netif;
static esp_netif_t *esp_netif_new(const esp_netif_config_t *c){
 assert(*c==ap_expected);allocated++;return failure==1?NULL:&netif;
}
static int attach(esp_netif_t *n,int ap){assert(ap==ap_expected);attached++;return failure==2?42:0;}
static int esp_netif_attach_wifi_station(esp_netif_t *n){return attach(n,0);}
static int esp_netif_attach_wifi_ap(esp_netif_t *n){return attach(n,1);}
static int install(int ap){assert(ap==ap_expected);handlers++;return failure==3?43:0;}
static int esp_wifi_set_default_wifi_sta_handlers(void){return install(0);}
static int esp_wifi_set_default_wifi_ap_handlers(void){return install(1);}
static void esp_netif_destroy_default_wifi(esp_netif_t *n){assert(n==&netif);destroyed++;}
'''+function(source,'create_wifi_netif')+r'''
int main(void){
 for(ap_expected=0;ap_expected<2;ap_expected++) for(failure=0;failure<4;failure++){
  allocated=attached=handlers=destroyed=0;esp_netif_t *result=NULL;
  int rc=create_wifi_netif(ap_expected,&result);
  assert(allocated==1);
  if(failure==0)assert(rc==0 && result==&netif && destroyed==0);
  else assert(rc!=0 && result==NULL);
  if(failure==1)assert(attached==0 && handlers==0 && destroyed==0);
  if(failure==2)assert(attached==1 && handlers==0 && destroyed==1);
  if(failure==3)assert(attached==1 && handlers==1 && destroyed==1);
 }
}
''')
        self.assertNotIn('esp_netif_create_default_wifi_',source)

    def test_wifi_errors_do_not_abort_recovery_console(self):
        source=(ROOT/'platforms/esp-idf/components/wifi_manager/src/wifi_manager.c').read_text()
        self.assertNotIn('ESP_ERROR_CHECK(',source)

    def test_bk_dispatch_and_registration(self):
        source=BK.read_text()
        run_c(r'''
#include <assert.h>
#include <stdbool.h>
#include <stdatomic.h>
#include <stdio.h>
#include <string.h>
#include "xiaotai_console_policy.h"
#define BK_OK 0
struct cli_command {const char *name,*help;void (*function)(char*,int,int,char**);};
static unsigned calls,registrations;
static int registration_error;
static void handler(char *out,int size,int argc,char **argv){calls++;}
static const struct cli_command s_commands[]={
 {"ntp-set","",handler},{"wifi-clear","",handler},{"tirtc-clear","",handler},
 {"ep-reset","",handler},{"restart","",handler},{"status","",handler},
 {"ai-start","",handler},{"log-level","",handler},{"wifi-change","",handler}};
static struct cli_command s_dispatch_commands[9];
static atomic_bool s_runtime_ready;
static bool s_registered;
static int cli_register_commands(const struct cli_command *c,int n){
 registrations++;assert(n==9);return registration_error;
}
'''+function(source,'response')+function(source,'xiaotai_console_set_runtime_ready')+
              function(source,'dispatch_command')+function(source,'xiaotai_console_start')+r'''
int main(void){
 registration_error=7;assert(xiaotai_console_start()==7);
 registration_error=0;assert(xiaotai_console_start()==0);
 assert(xiaotai_console_start()==0 && registrations==2);
 char output[160];char *args[]={NULL,"value"};
 for(int i=0;i<9;i++){
  args[0]=(char*)s_commands[i].name;unsigned before=calls;
  s_dispatch_commands[i].function(output,sizeof(output),2,args);
  if(i<5)assert(calls==before+1);
  else assert(calls==before && strstr(output,"not ready"));
 }
 xiaotai_console_set_runtime_ready(true);
 args[0]="ai-start";unsigned before=calls;
 dispatch_command(output,sizeof(output),1,args);assert(calls==before+1);
 xiaotai_console_set_runtime_ready(false);
 dispatch_command(output,sizeof(output),1,args);assert(calls==before+1);
}
''',[COMMON])

    def test_usb_constructor_failure_releases_only_its_driver(self):
        source=ESP.read_text()
        run_c(r'''
#include <assert.h>
#include <stdbool.h>
#include <stddef.h>
typedef int esp_err_t;
typedef int esp_console_repl_config_t;
typedef int esp_console_dev_usb_serial_jtag_config_t;
#define ESP_OK 0
#define ESP_CONSOLE_DEV_USB_SERIAL_JTAG_CONFIG_DEFAULT() 0
static void *s_repl;
static bool installed;
static int fail,uninstalls,vfs_restored;
static bool usb_serial_jtag_is_driver_installed(void){return installed;}
static void usb_serial_jtag_vfs_use_nonblocking(void){vfs_restored++;}
static int usb_serial_jtag_driver_uninstall(void){installed=false;uninstalls++;return 0;}
static int esp_console_new_repl_usb_serial_jtag(const int *d,const int *r,void **out){
 if(installed)return 8;
 installed=true;
 if(fail)return 9;
 *out=(void*)1;return 0;
}
'''+function(source,'create_usb_repl')+r'''
int main(void){
 int config=0;fail=1;assert(create_usb_repl(&config)==9);
 assert(!installed && uninstalls==1 && vfs_restored==1);
 fail=0;assert(create_usb_repl(&config)==0 && installed && s_repl);
 assert(create_usb_repl(&config)==8 && installed && uninstalls==1);
}
''')

    def test_esp_repl_retry_retains_waiting_task_until_started(self):
        s=ESP.read_text()
        run_c(r'''
#include <assert.h>
#include <stddef.h>
#include <stdbool.h>
static bool s_repl_started;
typedef int esp_err_t;
#define ESP_OK 0
typedef struct repl {int (*del)(struct repl*);} esp_console_repl_t;
static esp_console_repl_t *s_repl;
static int fail,deletes;
static int esp_console_start_repl(esp_console_repl_t *r){return fail;}
static int destroy(esp_console_repl_t *r){deletes++;return 0;}
''' + function(s,'start_repl') + r'''
int main(void){
 esp_console_repl_t repl={.del=destroy};
 s_repl=&repl;fail=9;assert(start_repl()==9 && s_repl==&repl && !s_repl_started && deletes==0);
 s_repl=&repl;fail=0;assert(start_repl()==0 && s_repl==&repl && s_repl_started && deletes==0);
}
''')

if __name__=='__main__':unittest.main()
