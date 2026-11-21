"""Fault injection through configuration save/load and factory-reset entry points."""
import unittest
from config_store_fixture import config_store_fixture
from test_s3_product_regressions import ROOT, function, run_c

class ConfigurationTransactions(unittest.TestCase):
    def test_factory_reset_rejects_late_binding_and_busy_reset_can_retry(self):
        r=(ROOT/'platforms/esp-idf/components/runtime_config/src/runtime_config.c').read_text()
        p=(ROOT/'platforms/esp-idf/components/starter_product/src/starter_product.c').read_text()
        run_c(config_store_fixture()+r'''
#include <assert.h>
#include <stdio.h>
#include "xiaotai_identity_gate.h"
typedef int esp_err_t;
typedef struct {int id;} runtime_tirtc_config_t;
#define ESP_OK 0
#define ESP_ERR_INVALID_STATE 3
#define PREFERENCES_SAVING 1
#define FACTORY_RESET_FAILED 2
#define ESP_LOGE(...) ((void)0)
#define ESP_LOGW(...) ((void)0)
#define pdMS_TO_TICKS(x) (x)
static xiaotai_identity_gate_t s_identity_gate;
static atomic_int s_preferences_save_status,s_factory_reset_status;
static int identity=1,rebooted;
static int save_tirtc_unlocked(const runtime_tirtc_config_t*v){identity=v->id;return 0;}
static int clear_tirtc_unlocked(void){identity=0;return 0;}
static int runtime_config_clear_endpoint(void){return 0;}
static int runtime_config_clear_ntp_server(void){return 0;}
static int wifi_manager_recover_credentials(const char *ssid,const char *password){return 0;}
static int clear_product_preferences(void){return 0;}
static void vTaskDelete(void*p){}
static void esp_restart(void){rebooted=1;}
'''+function(r,'runtime_config_clear_tirtc')+function(r,'runtime_config_save_tirtc')+function(r,'runtime_config_recover_tirtc')+r'''
static void vTaskDelay(int ms){config_task=20;runtime_tirtc_config_t late={9};assert(runtime_config_save_tirtc(&late)==ESP_ERR_INVALID_STATE);config_task=10;}
'''+function(p,'factory_reset_task')+r'''
int main(void){
 assert(config_store_write_begin());config_task=20;
 factory_reset_task(NULL);assert(!rebooted && identity==1);
 config_task=10;config_store_write_end();
 runtime_tirtc_config_t manual={7};assert(runtime_config_recover_tirtc(&manual)==0);
 factory_reset_task(NULL);
 assert(rebooted && identity==0);
 /* Even reuse of the former reset task handle cannot reopen frozen stores. */
 assert(!config_store_write_begin());
 config_store_reset_end(false); /* new-boot simulation */
 assert(config_store_reset_begin());config_store_reset_end(false);
 assert(config_store_write_begin());config_store_write_end();
}
''',[ROOT/'platforms/common/include'])

    def test_wifi_record_survives_each_write_failure_and_rejects_corruption(self):
        s=(ROOT/'platforms/esp-idf/components/wifi_manager/src/wifi_manager.c').read_text()
        run_c(config_store_fixture()+r'''
#include <assert.h>
#include <stdio.h>
#include <string.h>
typedef int esp_err_t,nvs_handle_t;
#define ESP_OK 0
#define ESP_ERR_INVALID_ARG 1
#define ESP_ERR_NVS_NOT_FOUND 2
#define ESP_ERR_INVALID_STATE 3
#define NVS_READONLY 0
#define NVS_READWRITE 1
#define WIFI_NVS_NAMESPACE "wifi_cfg"
#define WIFI_NVS_SSID "ssid"
#define WIFI_NVS_PASSWORD "password"
#include "xiaotai_identity_gate.h"
static xiaotai_identity_gate_t s_wifi_gate;
#define WIFI_RECORD_KEY "credentials_v1"
#define WIFI_MANAGER_SSID_MAX 32
#define WIFI_MANAGER_PASSWORD_MAX 64
#define ESP_LOGE(...) ((void)0)
typedef struct {char ssid[33],password[65];} wifi_manager_credentials_t;
typedef struct {uint32_t version;wifi_manager_credentials_t credentials;} wifi_record_t;
static wifi_record_t record;
static bool has_blob,has_ssid=true,has_password=true;
static int fail_at,writes,read_error;
static int nvs_open(const char*n,int mode,nvs_handle_t*h){*h=1;return 0;}
static void nvs_close(nvs_handle_t h){}
static int nvs_commit(nvs_handle_t h){return 0;}
static int nvs_get_blob(nvs_handle_t h,const char*k,void*v,size_t*n){if(!has_blob)return 2;memcpy(v,&record,sizeof(record));return 0;}
static int nvs_set_blob(nvs_handle_t h,const char*k,const void*v,size_t n){if(++writes==fail_at)return 55;memcpy(&record,v,n);has_blob=true;return 0;}
static int nvs_get_str(nvs_handle_t h,const char*k,char*v,size_t*n){if(read_error)return read_error;bool ssid=!strcmp(k,"ssid");if(!(ssid?has_ssid:has_password))return 2;strcpy(v,ssid?"old-ssid":"old-password");return 0;}
static int nvs_erase_key(nvs_handle_t h,const char*k){if(++writes==fail_at)return 55;if(!strcmp(k,"ssid"))has_ssid=false;else has_password=false;return 0;}
static int nvs_erase_all(nvs_handle_t h){has_blob=has_ssid=has_password=false;return 0;}
'''+''.join(function(s,n) for n in ['set_error','wifi_manager_credentials_valid','wifi_manager_load_credentials','save_credentials_unlocked','forget_credentials_unlocked','wifi_manager_save_credentials','wifi_manager_forget_credentials','wifi_manager_recover_credentials'])+r'''
int main(void){
 wifi_manager_credentials_t out;
 read_error=55;assert(wifi_manager_save_credentials("new-ssid","new-password")==55);
 assert(writes==0 && has_ssid && has_password);read_error=0;
 for(fail_at=1;fail_at<=4;fail_at++){
  has_blob=false;has_ssid=has_password=true;writes=0;
  assert(wifi_manager_save_credentials("new-ssid","new-password")==55);
  assert(wifi_manager_load_credentials(&out)==0);
  assert(!strcmp(out.ssid,"old-ssid") && !strcmp(out.password,"old-password"));
 }
 fail_at=0;assert(wifi_manager_save_credentials("new-ssid","new-password")==0);
 assert(wifi_manager_load_credentials(&out)==0 && !strcmp(out.ssid,"new-ssid") && !strcmp(out.password,"new-password"));
 assert(!has_ssid && !has_password);
 record.version=99;assert(wifi_manager_load_credentials(&out)!=0 && !out.ssid[0]);
 record.version=1;memset(record.credentials.password,'x',65);
 assert(wifi_manager_load_credentials(&out)!=0 && !out.password[0]);
 assert(config_store_reset_begin());config_task=20;
 assert(wifi_manager_save_credentials("late","late-password")==ESP_ERR_INVALID_STATE);
 assert(wifi_manager_forget_credentials()==ESP_ERR_INVALID_STATE);
 config_task=10;assert(wifi_manager_forget_credentials()==0);config_store_reset_end(true);
 assert(wifi_manager_load_credentials(&out)!=0);
}
''',[ROOT/'platforms/common/include'])

if __name__=='__main__':unittest.main()
