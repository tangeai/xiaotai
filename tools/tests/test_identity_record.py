from config_store_fixture import config_store_fixture
"""Exercise ESP persistence using NVS immediate-write semantics, not a fake transaction."""
import unittest
from test_s3_product_regressions import ROOT, function, run_c

class IdentityRecord(unittest.TestCase):
    def test_atomic_record_legacy_read_and_corruption(self):
        s=(ROOT/'platforms/esp-idf/components/runtime_config/src/runtime_config.c').read_text()
        run_c(config_store_fixture() + r'''
#include <assert.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "xiaotai_identity_gate.h"
typedef int esp_err_t;
typedef int nvs_handle_t;
#define ESP_OK 0
#define ESP_ERR_INVALID_ARG 1
#define ESP_ERR_NVS_NOT_FOUND 2
#define ESP_ERR_INVALID_STATE 3
#define NVS_READWRITE 1
#define NVS_READONLY 0
#define TIRTC_NVS_NAMESPACE "tirtc_cfg"
#define TIRTC_IDENTITY_KEY "identity_v1"
typedef struct {char device_id[65],device_secret[257],client_id[65];} runtime_tirtc_config_t;
typedef struct {uint32_t version;runtime_tirtc_config_t config;} runtime_identity_record_t;
static xiaotai_identity_gate_t s_identity_gate;
static runtime_tirtc_config_t legacy={.device_id="old-id",.device_secret="old-secret"};
static runtime_identity_record_t record;
static int failure,has_blob,blob_writes;
static bool legacy_id=true,legacy_secret=true,legacy_client=true;
static int nvs_open(const char*n,int mode,nvs_handle_t*h){*h=1;return 0;}
static int nvs_set_str(nvs_handle_t h,const char*k,const char*v){
 if(!strcmp(k,"device_id"))strcpy(legacy.device_id,v);
 if(!strcmp(k,"secret")){if(failure)return 55;strcpy(legacy.device_secret,v);}
 return 0;
}
static int nvs_get_str(nvs_handle_t h,const char*k,char*v,size_t*n){
 if((!strcmp(k,"device_id") && !legacy_id) || (!strcmp(k,"secret") && !legacy_secret) || (!strcmp(k,"client_id") && !legacy_client))return ESP_ERR_NVS_NOT_FOUND;
 const char *value=!strcmp(k,"device_id")?legacy.device_id:!strcmp(k,"secret")?legacy.device_secret:legacy.client_id;
 assert(strlen(value)<*n);strcpy(v,value);return 0;
}
static int nvs_set_blob(nvs_handle_t h,const char*k,const void*v,size_t n){
 assert(!strcmp(k,TIRTC_IDENTITY_KEY) && n==sizeof(record));
 if(failure==1 || (failure==3 && !strcmp(((const runtime_identity_record_t*)v)->config.device_id,"new-id")))return 55;
 memcpy(&record,v,n);has_blob=1;blob_writes++;return 0;
}
static int nvs_get_blob(nvs_handle_t h,const char*k,void*v,size_t*n){
 if(!has_blob)return ESP_ERR_NVS_NOT_FOUND;
 assert(*n==sizeof(record));memcpy(v,&record,sizeof(record));return 0;
}
static int nvs_erase_key(nvs_handle_t h,const char*k){
 if(failure==2)return 55;
 if(!strcmp(k,"device_id"))legacy_id=false;
 if(!strcmp(k,"secret"))legacy_secret=false;
 if(!strcmp(k,"client_id"))legacy_client=false;
 return 0;
}
static int nvs_commit(nvs_handle_t h){return 0;}
static void nvs_close(nvs_handle_t h){}
''' + '\n'.join(function(s,n) for n in ['set_error','runtime_config_tirtc_valid',
        'runtime_config_load_tirtc','save_tirtc_unlocked','runtime_config_save_tirtc']) + r'''
int main(void){
 runtime_tirtc_config_t v={.device_id="new-id",.device_secret="new-secret",.client_id="new-client"},read;
 assert(runtime_config_load_tirtc(&read)==0 && !strcmp(read.device_id,"old-id"));
 failure=1;assert(runtime_config_save_tirtc(&v)==55);
 assert(runtime_config_load_tirtc(&read)==0 && !strcmp(read.device_id,"old-id") && !strcmp(read.device_secret,"old-secret"));
 failure=2;assert(runtime_config_save_tirtc(&v)==55);
 assert(runtime_config_load_tirtc(&read)==0 && !strcmp(read.device_id,"old-id"));
 failure=3;assert(runtime_config_save_tirtc(&v)==55);
 assert(runtime_config_load_tirtc(&read)==0 && !strcmp(read.device_id,"old-id"));
 failure=0;assert(runtime_config_save_tirtc(&v)==0 && blob_writes>0);
 assert(runtime_config_load_tirtc(&read)==0 && !strcmp(read.device_id,"new-id") && !strcmp(read.device_secret,"new-secret"));
 failure=1;strcpy(v.device_id,"third-id");assert(runtime_config_save_tirtc(&v)==55);
 assert(runtime_config_load_tirtc(&read)==0 && !strcmp(read.device_id,"new-id"));
 record.version=99;assert(runtime_config_load_tirtc(&read)!=0 && read.device_id[0]==0);
 record.version=1;memset(record.config.device_secret,'x',sizeof(record.config.device_secret));
 assert(runtime_config_load_tirtc(&read)!=0 && read.device_id[0]==0);
 has_blob=0;assert(runtime_config_load_tirtc(&read)!=0 && read.device_id[0]==0);
 memset(&v,'x',sizeof(v));assert(!runtime_config_tirtc_valid(&v,NULL,0));
}
''',[ROOT/'platforms/common/include'])

if __name__=='__main__':unittest.main()
