"""Regression replay using production functions and controlled dependencies."""
import unittest
import sys
sys.path.insert(0,'tools/tests')
from test_s3_product_regressions import function,run_c
from pathlib import Path
s=Path('platforms/esp-idf/components/starter_runtime/src/starter_runtime.c').read_text()

class Regression(unittest.TestCase):
    def test_recovery(self):
        run_c(r'''
        #include <assert.h>
        #include <stdbool.h>
        #include <stdint.h>
        #include <stdio.h>
        #include <string.h>
        #include <stdlib.h>
        typedef int esp_err_t,platform_service_t,call_http_stage_t;
        #define ESP_OK 0
        #define ESP_ERR_INVALID_ARG 1
        #define ESP_ERR_TIMEOUT 2
        #define ESP_ERR_NO_MEM 3
        #define STARTER_PRODUCT_CONTACTS_MAX 8
        #define EVENT_CALL_DIAL 1
        #define pdMS_TO_TICKS(x) (x)
        #define pdTRUE 1
        #define ESP_LOGW(...) ((void)0)
        #define ESP_LOGI(...) ((void)0)
        #define CONFIG_IDF_TARGET_ESP32P4 0
        #define PLATFORM_SERVICE_CALL 0
        #define PLATFORM_SERVICE_VOIP 1
        #define CALL_HTTP_VOIP_DIAL 1
        #define CALL_HTTP_DEVICE_DIAL 2
        #define CALL_CONNECT_TIMEOUT_MS 10000
        #define XIAOTAI_CALL_CAMERA_ROTATION_JSON "0"
        typedef struct {int type,command;bool flag;size_t length;char *text;} runtime_event_t;
        typedef struct {int source;char id[65],wx_app_id[65],wx_model_id[65];} starter_product_contact_t;
        typedef struct {unsigned contact_count;starter_product_contact_t contacts[8];} starter_runtime_product_snapshot_t;
        static starter_runtime_product_snapshot_t s_product_snapshot;
        static starter_runtime_product_snapshot_t starter_runtime_product_snapshot(void){return s_product_snapshot;}
        static void product_set_call_result(const char *s){}
        static void *s_product_mutex=(void*)1;
        static runtime_event_t queued;
        static bool s_call_video,s_call_wechat;
        static char s_call_peer_name[65],s_device_id[65],s_call_wx_app_id[65],s_call_peer_id[65],s_call_wx_model_id[65],selected[65];
        static bool queue_event(const runtime_event_t *e){queued=*e;return true;}
        static int xSemaphoreTake(void *m,int t){return 1;}
        static void xSemaphoreGive(void *m){}
        static bool begin_call_common(const starter_product_contact_t *c){strcpy(selected,c->id);return false;}
        static void starter_media_set_call_video(bool b){}
        static void product_set_call(bool a,bool b,const char*c,bool d){}
        static void starter_tirtc_expect_call(unsigned n){}
        static unsigned session_generation(void){return 1;}
        static void call_http_response(const char *b,void *p){}
        static void *call_http_tag(unsigned g,int s){return NULL;}
        static int platform_client_request_timeout(int s,const char*p,const char*b,unsigned t,void(*f)(const char*,void*),void*ctx){return 0;}
        static void finish_call_session(int e,const char*t){}
        ''' + function(s,'starter_runtime_call_target')+function(s,'enqueue_contact_index')+function(s,'starter_runtime_call_contact')+function(s,'dial_contact')+function(s,'dial_selected_contact')+r'''
        int main(void){
         s_product_snapshot.contact_count=1;strcpy(s_product_snapshot.contacts[0].id,"Alice");
         assert(starter_runtime_call_contact(0)==0);
         /* A contact response already ahead of this event in the runtime queue. */
         strcpy(s_product_snapshot.contacts[0].id,"Bob");
         dial_selected_contact((const starter_product_contact_t*)queued.text,queued.flag);
         printf("UI intended Alice; call target selected: %s\n",selected);
         assert(selected[0]==0);
         s_product_snapshot.contacts[1]=*(starter_product_contact_t*)queued.text;
         s_product_snapshot.contact_count=2;
         dial_selected_contact((const starter_product_contact_t*)queued.text,queued.flag);
         assert(!strcmp(selected,"Alice"));free(queued.text);
        }
        ''')

if __name__ == "__main__": unittest.main()
