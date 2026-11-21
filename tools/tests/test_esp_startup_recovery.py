"""Regression replay using production functions and controlled dependencies."""
import unittest
import sys
sys.path.insert(0,'tools/tests')
from test_s3_product_regressions import function,run_c
from pathlib import Path
s=Path('platforms/esp-idf/main/app_main.c').read_text()

class Regression(unittest.TestCase):
    def test_recovery(self):
        run_c(r'''
        #include <assert.h>
        #include <stdbool.h>
        #include <stdint.h>
        #include <stdio.h>
        #include <string.h>
        #include <setjmp.h>
        #include <stddef.h>
        typedef int esp_err_t;
        #define ESP_OK 0
        #define ESP_ERR_NOT_FOUND 1
        #define ESP_ERR_NVS_NOT_FOUND 2
        #define START_RETRY_DELAY_MS 5000
        #define TIRTC_START_TIMEOUT_MS 30000
        #define TIRTC_INTERNAL_RESERVE_BYTES 20000
        #define CONFIG_XIAOTAI_TIRTC_MAX_SEND_BUFFER_BYTES 200000
        #define PLATFORM_REQUEST_TASK_STACK_BYTES 10000
        #define MALLOC_CAP_SPIRAM 1
        #define MALLOC_CAP_8BIT 2
        #define pdPASS 1
        #define pdMS_TO_TICKS(x) (x)
        #define ESP_LOGI(...) ((void)0)
        #define ESP_LOGW(...) ((void)0)
        #define ESP_LOGE(...) ((void)0)
        #define DISCOVERY_URL "test"
        typedef struct {char device_id[65],device_secret[257],client_id[65];} runtime_tirtc_config_t;
        static runtime_tirtc_config_t s_tirtc_config;
        static char s_discovery_base[256],s_discovery_url[256],s_tirtc_endpoint[256];
        static void *s_tirtc_internal_reserve;
        typedef struct {const char *device_id,*device_secret,*client_id,*mac_address,*discovery_url;} platform_client_config_t;
        typedef struct {const char *device_id,*device_secret,*client_id,*service_endpoint;unsigned max_send_buffer_bytes;int log_level;} starter_tirtc_config_t;
        static jmp_buf done;
        static int scenario,delays,starts,platform_checks,rebinds;
        static int64_t now;
        static int64_t esp_timer_get_time(void){return now;}
        static bool wifi_manager_connected(void){return true;}
        static void vTaskDelay(int ms){now+=(int64_t)ms*1000; if(++delays==400)longjmp(done,1);}
        static int station_identity(char *mac,char*id){strcpy(mac,"mac");strcpy(id,"client");return 0;}
        static int runtime_config_load_endpoint(char *p,size_t n){return 2;}
        static bool runtime_config_format_discovery_url(const char *s,char*d,size_t n){return false;}
        static int runtime_config_load_tirtc(runtime_tirtc_config_t *c){strcpy(c->device_id,"revoked-id");strcpy(c->device_secret,"old-secret");return 0;}
        static bool runtime_config_tirtc_valid(const runtime_tirtc_config_t*c,char*e,size_t n){return true;}
        static int provision_and_save(const char *mac,bool signed_rebind){rebinds++;return 0;}
        static int uxTaskGetStackHighWaterMark(void*p){return 1000;}
        static int starter_runtime_start(const char *id){return 0;}
        static void vTaskDelete(void*p){longjmp(done,3);}
        static void starter_console_set_runtime_ready(bool b){}
        static int platform_client_discover_tirtc_endpoint(const char*u,char*p,size_t n){strcpy(p,"endpoint");return 0;}
        static void log_heap_snapshot(const char *p){}
        static void heap_caps_free(void*p){}
        static int starter_tirtc_start(const starter_tirtc_config_t*c){starts++;return scenario==0?-1:0;}
        static bool starter_tirtc_started(void){return false;}
        static int starter_runtime_arm_external_connect_reserve(void){return 0;}
        static void on_local_voice_result(void){}
        static int starter_voice_start(void (*f)(void),void*p){return 0;}
        static bool platform_client_ready(void){return false;}
        static int platform_client_check_identity(const platform_client_config_t*c){platform_checks++;return scenario<2?ESP_ERR_NOT_FOUND:ESP_OK;}
        static int platform_client_start(const platform_client_config_t*c){return ESP_ERR_NOT_FOUND;}
        static void esp_restart(void){longjmp(done,2);}
        static int starter_button_start(void){return 0;}
        static void platform_request_task(void*p){}
        static int xTaskCreateWithCaps(void(*f)(void*),const char*n,int size,void*p,int priority,void*h,int flags){return pdPASS;}
        ''' + function(s,'starter_start_task') + r'''
        int main(void){
         for(scenario=0;scenario<3;scenario++){
          delays=starts=platform_checks=rebinds=0;now=0;
          if(setjmp(done)==0)starter_start_task(NULL);
          printf("scenario=%d TiRTC attempts=%d platform credential checks=%d rebinds=%d\n",scenario,starts,platform_checks,rebinds);
          assert(platform_checks>0);
          if(scenario<2)assert(rebinds==1 && starts==0);
          else assert(rebinds==0 && starts==1 && now>=30000000 && delays<400);
         }
        }
        ''')

if __name__ == "__main__": unittest.main()
