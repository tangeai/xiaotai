"""Replay the real credential erase API with the IDF cache-disabled stack guard."""
import re
import unittest
from test_s3_product_regressions import ROOT, run_c

SOURCE = ROOT / 'platforms/esp-idf/components/runtime_config/src/runtime_config.c'

class UnbindFlashStack(unittest.TestCase):
    def test_clear_from_psram_task_never_runs_nvs_on_external_stack(self):
        source = re.sub(r'^#include.*$', '', SOURCE.read_text(), flags=re.M)
        runtime = (ROOT / 'platforms/esp-idf/components/starter_runtime/src/starter_runtime.c').read_text()
        start = runtime.index('    if (strcmp(signal, "unbind") == 0) {')
        end = runtime.index('    if (strcmp(signal, "callers_update")', start)
        unbind = runtime[start:end]
        run_c(r'''
#include <assert.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#define ESP_OK 0
#define ESP_FAIL -1
#define ESP_ERR_INVALID_ARG 2
#define ESP_ERR_NO_MEM 3
#define ESP_ERR_NVS_NOT_FOUND 4
#define NVS_READONLY 1
#define NVS_READWRITE 2
#define pdPASS 1
#define pdTRUE 1
#define portMAX_DELAY 0xffffffffU
#define pdMS_TO_TICKS(x) (x)
#define MALLOC_CAP_INTERNAL 1
#define MALLOC_CAP_8BIT 2
#define MALLOC_CAP_SPIRAM 4
#define tskIDLE_PRIORITY 0
#define configMAX_PRIORITIES 25
typedef int esp_err_t;
typedef int nvs_handle_t;
typedef int BaseType_t;
typedef void *TaskHandle_t;
typedef void *SemaphoreHandle_t;
typedef struct { char device_id[65],device_secret[257],client_id[65]; } runtime_tirtc_config_t;
static bool internal_stack, semaphore_ready, create_fail, sem_fail;
static int erase_result, commit_result, opens, erases, commits, closes, workers;
static bool esp_task_stack_is_sane_cache_disabled(void){return internal_stack;}
static bool esp_ptr_internal(const void *pointer){return internal_stack;}
static esp_err_t nvs_open(const char *name,int mode,nvs_handle_t *handle){
 assert(esp_task_stack_is_sane_cache_disabled() && "spi_flash_disable_interrupts_caches_and_other_cpu cache_utils.c:127");
 assert(!strcmp(name,"tirtc_cfg"));opens++;*handle=1;return ESP_OK;
}
static int nvs_get_str(int h,const char*k,char*v,size_t*n){return ESP_ERR_NVS_NOT_FOUND;}
static int nvs_set_str(int h,const char*k,const char*v){return ESP_OK;}
static int nvs_erase_key(int h,const char*k){return ESP_OK;}
static int nvs_erase_all(int h){assert(internal_stack);erases++;return erase_result;}
static int nvs_commit(int h){assert(internal_stack);commits++;return commit_result;}
static void nvs_close(int h){closes++;}
static SemaphoreHandle_t xSemaphoreCreateBinary(void){return sem_fail?NULL:(void*)1;}
static int xSemaphoreGive(SemaphoreHandle_t s){semaphore_ready=true;return pdTRUE;}
static int xSemaphoreTake(SemaphoreHandle_t s,unsigned timeout){assert(semaphore_ready);semaphore_ready=false;return pdTRUE;}
static void vSemaphoreDelete(SemaphoreHandle_t s){}
static void vTaskDelete(void *task){}
static unsigned uxTaskPriorityGet(void *task){return 6;}
static int xTaskCreate(void(*fn)(void*),const char*name,unsigned stack,void*arg,unsigned priority,TaskHandle_t *task){
 if(create_fail)return 0;
 assert(stack>=4096 && stack<=8192);workers++;
 bool previous=internal_stack;internal_stack=true;fn(arg);internal_stack=previous;return pdPASS;
}
''' + source + r'''
#define ESP_LOGW(...) ((void)0)
#define ESP_LOGE(...) ((void)0)
static int finished, restarted;
static void cJSON_Delete(void *root){}
static void finish_session(int error){assert(error==0);finished++;}
static void vTaskDelay(unsigned ticks){}
static void esp_restart(void){assert(erases==1 && commits==1 && closes==1);restarted++;}
static void replay_platform_unbind(void){
 const char *signal="unbind";void *root=NULL;
''' + unbind + r'''
}
int main(void){
 replay_platform_unbind();
 assert(finished==1 && restarted==1);
 assert(workers==1 && erases==1 && commits==1 && closes==1);
 erase_result=ESP_FAIL;
 assert(runtime_config_clear_tirtc()==ESP_FAIL);
 assert(commits==1 && closes==2);
 erase_result=ESP_OK;commit_result=ESP_FAIL;
 assert(runtime_config_clear_tirtc()==ESP_FAIL);
 assert(commits==2 && closes==3);
 int before=opens;create_fail=true;
 assert(runtime_config_clear_tirtc()==ESP_ERR_NO_MEM && opens==before);
 create_fail=false;sem_fail=true;
 assert(runtime_config_clear_tirtc()==ESP_ERR_NO_MEM && opens==before);
 sem_fail=false;commit_result=ESP_OK;internal_stack=true;
 assert(runtime_config_clear_tirtc()==ESP_OK && workers==3);
}
''')

if __name__ == '__main__': unittest.main()
