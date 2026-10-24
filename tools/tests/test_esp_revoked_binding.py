"""Confirm the existing ESP token rejection and signed rebind behavior."""
import unittest
from test_s3_product_regressions import ROOT, run_c, function
from test_esp_signal_recovery import replay

class EspRevokedBinding(unittest.TestCase):
    def test_existing_platform_start_classifies_real_json(self):
        source=(ROOT/'platforms/esp-idf/components/platform_client/src/platform_client.c').read_text()
        replay(r'''
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <cJSON.h>
typedef int esp_err_t;
#define ESP_OK 0
#define ESP_FAIL -1
#define ESP_ERR_NOT_FOUND 5
#define ESP_ERR_INVALID_ARG 6
#define ESP_ERR_INVALID_SIZE 7
#define ESP_ERR_NO_MEM 8
#define ESP_ERR_INVALID_RESPONSE 9
#define PLATFORM_DEFAULT_DISCOVERY "test"
#define PLATFORM_HTTP_BODY_MAX 512
#define PLATFORM_DEFAULT_HTTP_TIMEOUT_MS 1000
#define ESP_LOGI(...) ((void)0)
#define ESP_LOGW(...) ((void)0)
#define ESP_LOGE(...) ((void)0)
typedef struct {const char *device_id,*device_secret,*mac_address,*discovery_url;} platform_client_config_t;
static bool s_ready,s_services_ready,s_binding_required;
static int mqtt_starts;
static int start_mqtt(void){mqtt_starts++;return 0;}
static char s_device_id[65],s_device_secret[257],s_mac_address[18],s_client_id[80],s_mqtt_token[64];
static struct {char device[64];} s_services={"http://test"};
static int wire_status=410,wire_rc;
static const char *wire_body="{\"code\":6006,\"msg\":\"revoked\"}";
static int sync_clock(void){return 0;}
static int discover_services(const char*s){s_services_ready=true;return 0;}
static unsigned long esp_random(void){return 123;}
static int hmac_signature(const char*t,char*s,size_t n){snprintf(s,n,"signature");return 0;}
static char *http_response_alloc(void){return malloc(512);}
#define heap_caps_free free
static int http_request(const char*u,const char*b,const char*bearer,const char**names,const char**values,size_t count,char*out,size_t n,unsigned timeout,int*status){
 assert(strstr(u,"/v1/device/token") && b && !b[0] && count==5);
 *status=wire_status;snprintf(out,n,"%s",wire_body);return wire_rc;
}
static bool json_copy_string(const cJSON*r,const char*k,char*out,size_t n){
 const cJSON*v=cJSON_GetObjectItemCaseSensitive(r,k);
 if(!cJSON_IsString(v)||!v->valuestring||strlen(v->valuestring)>=n)return false;
 snprintf(out,n,"%s",v->valuestring);return true;
}
'''+function(source,'obtain_mqtt_token')+function(source,'platform_client_start')+r'''
int main(void){
 platform_client_config_t config={"old-id","old-key","00:00:00:00:00:01","test"};
 assert(platform_client_start(&config)==ESP_ERR_NOT_FOUND);
 assert(!s_ready && mqtt_starts==0);
 wire_body="{\"code\":6001}";wire_status=401;
 assert(platform_client_start(&config)==ESP_ERR_INVALID_RESPONSE);
 wire_rc=-1;assert(platform_client_start(&config)==-1);wire_rc=0;
 wire_body="{\"code\":200,\"data\":{\"mqtt_token\":\"test-token\"}}";wire_status=200;
 assert(platform_client_start(&config)==ESP_OK);
 assert(s_ready && mqtt_starts==1 && !strcmp(s_mqtt_token,"test-token"));
}
''')

    def test_existing_startup_rebinds_only_for_revoked_credentials(self):
        source=(ROOT/'platforms/esp-idf/main/app_main.c').read_text()
        branch=source[source.index('        if (!platform_client_ready()) {'):]
        branch=branch[:branch.index('        if (platform_client_ready() && tirtc_submitted)')]
        run_c(r'''
#include <assert.h>
#include <stdbool.h>
#define ESP_OK 0
#define ESP_ERR_NOT_FOUND 5
#define ESP_LOGI(...) ((void)0)
#define ESP_LOGW(...) ((void)0)
#define ESP_LOGE(...) ((void)0)
#define pdMS_TO_TICKS(x) (x)
typedef int esp_err_t;
static int platform,token_result=ESP_ERR_NOT_FOUND,save_result,binds,restarts,delays;
static const char *mac_address="mac";
static bool platform_client_ready(void){return false;}
static int platform_client_start(const int*c){return token_result;}
static int provision_and_save(const char*m,bool signed_rebind){assert(signed_rebind);binds++;return save_result;}
static void vTaskDelay(unsigned n){assert(n==1000);delays++;}
static void esp_restart(void){restarts++;}
static void boot_platform_step(void){
'''+branch+r'''
}
int main(void){
 boot_platform_step();assert(binds==1 && restarts==1 && delays==1);
 save_result=-1;boot_platform_step();assert(binds==2 && restarts==1);
 token_result=-1;boot_platform_step();assert(binds==2 && restarts==1);
 token_result=ESP_OK;boot_platform_step();assert(binds==2 && restarts==1);
}
''')

if __name__=='__main__':unittest.main()
