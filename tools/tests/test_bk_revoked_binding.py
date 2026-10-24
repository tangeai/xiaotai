"""Replay revoked credentials through the production BK boot path."""
import unittest
from test_s3_product_regressions import ROOT, function, run_c

PLATFORM = ROOT / 'platforms/beken/runtime/src/xiaotai_platform_client.c'
APP = ROOT / 'platforms/beken/runtime/src/xiaotai_app.c'

PRELUDE = r'''
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define BK_OK 0
#define BK_FAIL -1
#define BK_ERR_PARAM -2
#define XIAOTAI_PLATFORM_ERR_UNBOUND -6006
#define BK_LOGI(...) ((void)0)
#define BK_LOGW(...) ((void)0)
#define BK_LOGE(tag, ...) ((void)printf(__VA_ARGS__))
#define BK_LOGD(...) ((void)0)
#define MQTT_TOKEN_MAX 64
#define HTTP_JSON_POST_EMPTY 2
#define HTTP_JSON_POST 1
typedef struct {char device[64];} platform_services_t;
typedef struct {char device_id[65],device_secret[257],client_id[65];} xiaotai_device_credentials_t;
typedef struct {int valueint; const char *valuestring;} cJSON;
static cJSON root,code,message={0,"revoked"},data;
static int response_code=6006, response_status=410, profiles, cached;
static int format_device_mac(char *p){strcpy(p,"00:00:00:00:00:01");return 0;}
static long long xiaotai_time_now(void){return 1792833862;}
static int bk_fill_rand(void *p,size_t n){memset(p,1,n);return 0;}
static unsigned rtos_get_time(void){return 1;}
static int hmac_signature(const char*k,const char*t,char*s,size_t n){snprintf(s,n,"signature");return 0;}
static int http_json_request_ex(const char*u,int method,const char*b,const char*t,const char**names,const char**values,size_t n,char**out,size_t*sz,int*status){
 assert(strstr(u,"/v1/device/token"));assert(method==HTTP_JSON_POST_EMPTY && n==5);
 *out=malloc(3);strcpy(*out,"{}");*sz=2;if(status)*status=response_status;
 return response_status==200?BK_OK:BK_FAIL;
}
static cJSON *cJSON_Parse(const char*s){code.valueint=response_code;return &root;}
static const cJSON *cJSON_GetObjectItemCaseSensitive(const cJSON*r,const char*n){
 if(!strcmp(n,"code"))return &code;
 if(!strcmp(n,"msg"))return &message;
 return &data;
}
static bool cJSON_IsNumber(const cJSON*p){return p==&code;}
static bool cJSON_IsObject(const cJSON*p){return p==&data;}
static bool cJSON_IsString(const cJSON*p){return p==&message;}
static void cJSON_Delete(cJSON*p){}
#define os_free free
static bool copy_json_string(const cJSON*r,const char*k,char*out,size_t n){snprintf(out,n,"token");return true;}
static int discover_services_with_network_time(platform_services_t*s){strcpy(s->device,"http://test");return 0;}
static int report_capabilities_with_token(const platform_services_t*s,const char*t){profiles++;return 0;}
static int cache_services(const platform_services_t*s,const char*t){cached++;return 0;}
'''

class RevokedBinding(unittest.TestCase):
    def test_token_rejection_survives_public_preparation_boundary(self):
        source=PLATFORM.read_text()
        run_c(PRELUDE + function(source,'obtain_device_token') + function(source,'xiaotai_platform_report_capabilities') + r'''
int main(void){
 xiaotai_device_credentials_t c={"old-id","old-key","client"};
 assert(xiaotai_platform_report_capabilities(&c)==XIAOTAI_PLATFORM_ERR_UNBOUND);
 assert(!profiles && !cached);
 response_code=6001;response_status=401;
 assert(xiaotai_platform_report_capabilities(&c)==BK_FAIL);
 response_code=200;response_status=500;
 assert(xiaotai_platform_report_capabilities(&c)==BK_FAIL);
 response_status=200;
 assert(xiaotai_platform_report_capabilities(&c)==BK_OK && profiles==1 && cached==1);
}
''')

    def test_signed_grant_can_retain_existing_credentials(self):
        run_c(r'''
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>
typedef struct {char device_id[65],device_secret[257],client_id[65];} xiaotai_device_credentials_t;
typedef struct {const char *valuestring;} cJSON;
static cJSON root,type={"auth_grant"},data,id={"new-id"},key={"new-key"};
static int fields;
static cJSON *cJSON_ParseWithLength(const char*s,size_t n){return &root;}
static const cJSON *cJSON_GetObjectItemCaseSensitive(const cJSON*r,const char*n){
 if(!strcmp(n,"type"))return &type;
 if(!strcmp(n,"payload"))return &data;
 if(!strcmp(n,"device_id") && fields>=1)return &id;
 if(!strcmp(n,"device_key") && fields>=2)return &key;
 return NULL;
}
static bool cJSON_IsString(const cJSON*p){return p && p->valuestring;}
static bool cJSON_IsObject(const cJSON*p){return p==&data;}
static void cJSON_Delete(cJSON*p){}
static bool copy_json_string(const cJSON*r,const char*k,char*out,size_t n){
 const cJSON *v=cJSON_GetObjectItemCaseSensitive(r,k);
 if(!cJSON_IsString(v)||strlen(v->valuestring)>=n)return false;
 snprintf(out,n,"%s",v->valuestring);return true;
}
'''+function(PLATFORM.read_text(),'parse_auth_grant')+r'''
int main(void){
 xiaotai_device_credentials_t old={"old-id","old-key","client"},c=old,empty={0};
 assert(parse_auth_grant((const uint8_t*)"grant",5,&c));
 assert(!strcmp(c.device_id,"old-id") && !strcmp(c.device_secret,"old-key"));
 assert(!parse_auth_grant((const uint8_t*)"grant",5,&empty));
 fields=1;c=old;assert(!parse_auth_grant((const uint8_t*)"grant",5,&c));
 assert(!strcmp(c.device_id,"old-id"));
 fields=2;assert(parse_auth_grant((const uint8_t*)"grant",5,&c));
 assert(!strcmp(c.device_id,"new-id") && !strcmp(c.device_secret,"new-key"));
}
''')

    def test_public_rebind_signs_report_and_seeds_grant_identity(self):
        prelude=PRELUDE.replace('static int response_code=6006', 'static int response_code=200')
        prelude=prelude.replace('response_status=410', 'response_status=200')
        prelude=prelude.replace('snprintf(s,n,"signature");return 0;',
            'assert(!strcmp(k,"old-key"));assert(strstr(t,"old-id1792833862")==t);snprintf(s,n,"signature");return 0;')
        prelude=prelude.replace('assert(strstr(u,"/v1/device/token"));assert(method==HTTP_JSON_POST_EMPTY && n==5);',
            r'''assert(strstr(u,"/v1/device/report"));assert(method==HTTP_JSON_POST && b);
 assert(n==4 && !strcmp(names[0],"X-Device-Id") && !strcmp(values[0],"old-id"));
 assert(!strcmp(names[1],"X-Timestamp") && !strcmp(names[2],"X-Nonce"));
 assert(!strcmp(names[3],"X-Signature") && !strcmp(values[3],"signature"));''')
        prelude=prelude.replace('snprintf(out,n,"token")', 'snprintf(out,n,"123456")')
        source=PLATFORM.read_text()
        run_c(prelude+r'''
typedef struct {char code[7],temp_token[64],temp_client_id[64];} provision_report_t;
static bool s_binding;
static unsigned s_verification_expiry;
static char s_verification_code[7];
static unsigned xiaotai_binding_token_expiry(const char*t){return 180;}
static int wait_for_auth_grant(const platform_services_t*s,const provision_report_t*r,xiaotai_device_credentials_t*out){
 assert(s_binding && !strcmp(r->code,"123456"));
 assert(!strcmp(out->device_id,"old-id") && !strcmp(out->device_secret,"old-key"));return 0;
}
static void xiaotai_audio_cancel_prompt(void){}
'''+function(source,'report_device')+function(source,'bind_device')+function(source,'xiaotai_platform_rebind')+r'''
int main(void){
 xiaotai_device_credentials_t old={"old-id","old-key","client"},out={0};
 assert(xiaotai_platform_rebind(&old,&out)==BK_OK);
 assert(!s_binding && !s_verification_expiry && !strcmp(out.device_id,"old-id"));
 assert(!strcmp(old.device_secret,"old-key"));
 assert(xiaotai_platform_rebind(&old,&old)==BK_ERR_PARAM);
 (void)profiles;(void)cached;
}
''')

    def test_boot_rebinds_before_retrying_revoked_identity(self):
        source=PLATFORM.read_text(); app=APP.read_text()
        boot=function(app,'supervisor_task')
        boot=boot[:boot.index('    char tirtc_endpoint[256];')]+'}\n'
        run_c(PRELUDE + function(source,'obtain_device_token') + function(source,'xiaotai_platform_report_capabilities') + r'''
typedef int beken_thread_arg_t;
static char s_device_id[65];static int s_room, delays, binds, saves;
static bool xiaotai_network_ready(void){return true;}
static void rtos_delay_milliseconds(unsigned n){assert(++delays<4 && "revoked credentials retry forever instead of binding");}
static int xiaotai_storage_load_device(xiaotai_device_credentials_t*c){strcpy(c->device_id,"old-id");strcpy(c->device_secret,"old-key");return 0;}
static int xiaotai_platform_bind(xiaotai_device_credentials_t*c){assert(0 && "must sign rebind with saved identity");return -1;}
static int xiaotai_platform_rebind(const xiaotai_device_credentials_t*old,xiaotai_device_credentials_t*c){
 assert(!strcmp(old->device_id,"old-id") && !strcmp(old->device_secret,"old-key"));
 binds++;strcpy(c->device_id,"new-id");strcpy(c->device_secret,"new-key");return 0;
}
static int xiaotai_storage_save_device(const xiaotai_device_credentials_t*c){
 assert(!strcmp(c->device_id,"new-id"));saves++;if(saves==1)return BK_FAIL;
 response_code=response_status=200;return 0;
}
static void xiaotai_ui_show_status(const char*s){assert(!strcmp(s,"BOUND") && saves==2);}
static void xiaotai_room_set_device_id(int*r,const char*s){}
''' + boot + r'''
int main(void){supervisor_task(0);assert(binds==2 && saves==2 && profiles==1 && cached==1);assert(!strcmp(s_device_id,"new-id"));}
''')

if __name__ == '__main__': unittest.main()
