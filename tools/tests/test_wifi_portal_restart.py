import unittest
from pathlib import Path
import sys
sys.path.insert(0,'tools/tests')
from test_s3_product_regressions import function,run_c
s=Path('platforms/esp-idf/components/wifi_manager/src/wifi_manager.c').read_text()

class PortalRestart(unittest.TestCase):
    def test_task_failure_and_duplicate_post(self):
        run_c(r'''
        #include <assert.h>
        #include <stdbool.h>
        #include <stddef.h>
        #include <stdio.h>
        #include <string.h>
        typedef int esp_err_t;
        typedef struct {int content_len;} httpd_req_t;
        typedef struct {const char *valuestring;} cJSON;
        #define ESP_OK 0
        #define pdPASS 1
        #define HTTPD_503_SERVICE_UNAVAILABLE 503
        static bool s_restart_scheduled;
        static int task_result;
        #define HTTPD_400_BAD_REQUEST 400
        #define HTTPD_408_REQ_TIMEOUT 408
        #define HTTPD_500_INTERNAL_SERVER_ERROR 500
        #define HTTPD_SOCK_ERR_TIMEOUT -2
        static cJSON root,ssid={"ssid"},password={"password"};
        static int saves,restart_attempts;static char response[128];
        static int httpd_req_recv(httpd_req_t*r,char*b,size_t n){memcpy(b,"{}",2);return 2;}
        static int httpd_resp_send_err(httpd_req_t*r,int code,const char*s){return code;}
        static cJSON *cJSON_ParseWithLength(const char*b,size_t n){return &root;}
        static const cJSON *cJSON_GetObjectItemCaseSensitive(cJSON*r,const char*k){return !strcmp(k,"ssid")?&ssid:&password;}
        static bool cJSON_IsString(const cJSON*p){return p!=NULL;}
        static void cJSON_Delete(cJSON*p){}
        static bool wifi_manager_credentials_valid(const char*s,const char*p,char*e,size_t n){return true;}
        static int wifi_manager_save_credentials(const char*s,const char*p){saves++;return 0;}
        static void httpd_resp_set_type(httpd_req_t*r,const char*t){}
        static int httpd_resp_sendstr(httpd_req_t*r,const char*s){snprintf(response,sizeof(response),"%s",s);return 0;}
        static void restart_task(void*p){}
        static int xTaskCreate(void(*f)(void*),const char*n,int bytes,void*p,int priority,void*h){restart_attempts++;return task_result;}
        '''+function(s,'wifi_config_post')+r'''
        int main(void){httpd_req_t request={2};int rc=wifi_config_post(&request);
         assert(rc==500 && saves==1 && restart_attempts==1 && response[0]==0);
         task_result=1;assert(wifi_config_post(&request)==0 && saves==2 && restart_attempts==2);
         assert(wifi_config_post(&request)==503 && saves==2 && restart_attempts==2);
        }
        ''')

if __name__=="__main__":unittest.main()
