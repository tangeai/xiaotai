"""Run production onboarding entry points with deterministic SDK fault injection."""
import unittest
from test_s3_product_regressions import ROOT, function, run_c

PLATFORM = ROOT / 'platforms/esp-idf/components/platform_client/src/platform_client.c'
MAIN = ROOT / 'platforms/esp-idf/main/app_main.c'

class OnboardingFaults(unittest.TestCase):
    def test_sntp_timeout_retry_does_not_reinitialize_service(self):
        run_c(r'''
#include <assert.h>
#include <stdbool.h>
#include <time.h>
typedef int esp_err_t;
typedef int esp_sntp_config_t;
#define ESP_OK 0
#define ESP_ERR_INVALID_STATE 1
#define ESP_ERR_TIMEOUT 2
#define ESP_NETIF_SNTP_DEFAULT_CONFIG_MULTIPLE(...) 0
#define pdMS_TO_TICKS(x) (x)
#define ESP_LOGI(...) ((void)0)
#define ESP_LOGE(...) ((void)0)
static bool s_sntp_initialized;
static unsigned inits,waits,init_error;
static time_t now;
static time_t fake_time(time_t*p){return now;}
#define time fake_time
static int esp_netif_sntp_init(const int*c){inits++;return init_error;}
static int esp_netif_sntp_sync_wait(unsigned ms){assert(ms==2000);waits++;return ESP_ERR_TIMEOUT;}
''' + function(PLATFORM.read_text(), 'sync_clock') + r'''
int main(void){
 assert(sync_clock()==ESP_ERR_TIMEOUT && inits==1 && waits==5);
 now=1791528424;assert(sync_clock()==ESP_OK && inits==1);
 assert(sync_clock()==ESP_OK && inits==1);
 s_sntp_initialized=false;inits=0;init_error=7;
 assert(sync_clock()==7 && inits==1);
 init_error=0;assert(sync_clock()==ESP_OK && inits==2);
 assert(sync_clock()==ESP_OK && inits==2);
}
''')

    def test_storage_failure_preserves_identity_for_signed_retry(self):
        run_c(r'''
#include <assert.h>
#include <stdbool.h>
#include <stdatomic.h>
#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>
typedef int esp_err_t;
#define ESP_OK 0
#define DISCOVERY_URL "test"
#define ESP_LOGI(...) ((void)0)
typedef struct {char device_id[65],device_secret[257],client_id[65];} runtime_tirtc_config_t;
static runtime_tirtc_config_t s_tirtc_config={"old-id","old-key","client"};
static atomic_bool s_verification_prompt_cancelled;
typedef struct {char device_id[65],device_secret[257];} platform_provision_result_t;
typedef struct {const char *mac_address,*existing_device_id,*existing_device_secret,*discovery_url;unsigned timeout_seconds;esp_err_t(*prompt_callback)(const int16_t*,size_t,void*);void(*prompt_cancel_callback)(void*);void*prompt_user_data;} platform_provision_config_t;
static unsigned save_error,feedback,attempts;
static int play_verification_prompt(const int16_t*p,size_t n,void*u){return 0;}
static void cancel_verification_prompt(void*u){}
static int platform_client_provision(const platform_provision_config_t*c,platform_provision_result_t*r){
 assert(!strcmp(c->existing_device_id,"old-id") && !strcmp(c->existing_device_secret,"old-key"));
 attempts++;snprintf(r->device_id,sizeof(r->device_id),"new-id");snprintf(r->device_secret,sizeof(r->device_secret),"new-key");return 0;
}
static int runtime_config_save_tirtc(const runtime_tirtc_config_t*c){
 assert(!strcmp(c->device_id,"new-id") && !strcmp(c->device_secret,"new-key") && !strcmp(c->client_id,"client"));return save_error;
}
static void starter_product_binding_saved(void){feedback++;}
''' + function(MAIN.read_text(), 'provision_and_save') + r'''
int main(void){save_error=7;assert(provision_and_save("mac",true)==7);
 assert(!strcmp(s_tirtc_config.device_id,"old-id") && !strcmp(s_tirtc_config.device_secret,"old-key") && !feedback);
 save_error=0;assert(provision_and_save("mac",true)==0 && attempts==2 && feedback==1);
 assert(!strcmp(s_tirtc_config.device_id,"new-id") && !strcmp(s_tirtc_config.device_secret,"new-key"));
}
''')

    def test_repeated_grant_keeps_original_ack_and_credentials(self):
        run_c(r'''
#include <assert.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>
#define ESP_LOGE(...) ((void)0)
#define PROVISION_ERROR_BIT 2
#define PROVISION_DONE_BIT 1
typedef struct {const char *valuestring;int kind;} cJSON;
static cJSON root={0,2}, type={"auth_grant",1},payload={0,2},id={"id-a",1},key={"key-a",1};
static cJSON *cJSON_ParseWithLength(const char*s,size_t n){return &root;}
static cJSON *cJSON_GetObjectItemCaseSensitive(const cJSON*r,const char*n){
 if(!strcmp(n,"type"))return &type;
 if(!strcmp(n,"payload"))return &payload;
 if(!strcmp(n,"device_id"))return &id;
 if(!strcmp(n,"device_key"))return &key;
 return 0;
}
static bool cJSON_IsString(const cJSON*p){return p && p->kind==1;}
static bool cJSON_IsObject(const cJSON*p){return p && p->kind==2;}
static void cJSON_Delete(cJSON*p){}
typedef struct {void *events;int mqtt,ack_message_id;char device_id[65],device_secret[257],temp_client_id[65];} provision_mqtt_t;
static unsigned publishes,errors;
static void xEventGroupSetBits(void *events,unsigned bits){errors++;}
static int esp_mqtt_client_publish(int mqtt,const char*topic,const char*msg,int len,int qos,int retain){publishes++;return 10+publishes;}
''' + function(PLATFORM.read_text(), 'provision_finish_with_error') + function(PLATFORM.read_text(), 'provision_handle_message') + r'''
int main(void){provision_mqtt_t ctx={.events=(void*)1,.mqtt=1,.ack_message_id=-1};
 provision_handle_message(&ctx,"grant",5);assert(publishes==1 && ctx.ack_message_id==11 && !strcmp(ctx.device_id,"id-a"));
 id.valuestring="id-b";key.valuestring="key-b";
 for(unsigned i=0;i<100;i++)provision_handle_message(&ctx,"grant",5);
 assert(publishes==1 && ctx.ack_message_id==11 && !strcmp(ctx.device_id,"id-a") && !strcmp(ctx.device_secret,"key-a") && !errors);
}
''')

    def test_binding_page_is_not_released_before_storage_or_online(self):
        source = PLATFORM.read_text()
        run_c(r'''
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>
#define ESP_OK 0
#define ESP_ERR_INVALID_ARG 1
#define PLATFORM_DEFAULT_DISCOVERY "test"
#define EXPERIENCE_PLATFORM_URL "test"
#define ESP_LOGW(...) ((void)0)
#define ESP_LOGI(...) ((void)0)
typedef int esp_err_t;
typedef struct {const char *mac_address,*existing_device_id,*existing_device_secret,*discovery_url;} platform_provision_config_t;
typedef struct {char device_id[65],device_secret[257];} platform_provision_result_t;
typedef struct {char code[17],temp_token[1024],temp_client_id[65];} provision_report_t;
static char s_mac_address[18],s_verification_code[17];
static bool s_binding_required,s_services_ready=true,s_provisioning;
static uint32_t s_verification_expiry;
static int grant_error;
static int sync_clock(void){return 0;}
static int discover_services(const char*s){return 0;}
static int report_for_provision(const platform_provision_config_t*c,provision_report_t*r){strcpy(r->code,"012345");return 0;}
static uint32_t xiaotai_binding_token_expiry(const char*t){return 190;}
static int wait_for_auth_grant(const provision_report_t*r,const platform_provision_config_t*c,platform_provision_result_t*out){return grant_error;}
static void mbedtls_platform_zeroize(void*p,size_t n){memset(p,0,n);}
''' + function(source, 'platform_client_provision') + r'''
int main(void){platform_provision_config_t c={.mac_address="mac"};platform_provision_result_t result;
 assert(platform_client_provision(&c,&result)==ESP_OK);
 assert(s_binding_required && !s_provisioning); /* Credentials are not persisted yet. */
 grant_error=7;assert(platform_client_provision(&c,&result)==7 && s_binding_required);
}
''')

if __name__ == '__main__': unittest.main()
