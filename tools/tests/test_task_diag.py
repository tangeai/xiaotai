"""Replay on-demand CPU sampling through the actual console command and ISR."""
import unittest
from test_s3_product_regressions import ROOT, function, run_c

SOURCE = ROOT / "platforms/esp-idf/components/starter_console/src/starter_console.c"
PREAMBLE = r'''
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include <stdbool.h>
static char output[8192];
static int out(const char *fmt,...){
 va_list ap;va_start(ap,fmt);int n=vsnprintf(output+strlen(output),sizeof(output)-strlen(output),fmt,ap);va_end(ap);return n;
}
#define printf out
static void print_firmware_identity(void){}
'''

class TaskDiag(unittest.TestCase):
    def test_tick_samples_are_per_core_bounded_and_hooks_are_removed(self):
        s=SOURCE.read_text()
        declarations=s[s.index("#define TASK_SAMPLE_COUNT"):s.index("static void IRAM_ATTR task_sample_tick")]
        run_c(PREAMBLE+r'''
#define CONFIG_IDF_TARGET_ESP32S3 1
#define CONFIG_FREERTOS_NUMBER_OF_CORES 2
#define IRAM_ATTR
#define DRAM_ATTR
#define ESP_OK 0
#define pdMS_TO_TICKS(x) (x)
#define portMUX_INITIALIZER_UNLOCKED 0
#define portENTER_CRITICAL(x) ((void)(x))
#define portEXIT_CRITICAL(x) ((void)(x))
#define portENTER_CRITICAL_ISR(x) ((void)(x))
#define portEXIT_CRITICAL_ISR(x) ((void)(x))
typedef void *TaskHandle_t;
typedef int portMUX_TYPE;
typedef int esp_err_t;
static unsigned core,delays,removed;
static int fail_core=-1;
static void (*hooks[2])(void);
static TaskHandle_t current;
static int64_t now=1000000;
static int64_t esp_timer_get_time(void){return now;}
static unsigned xPortGetCoreID(void){return core;}
static TaskHandle_t xTaskGetCurrentTaskHandleForCore(unsigned c){assert(c==core);return current;}
static TaskHandle_t xTaskGetHandle(const char *name){
 if(!strcmp(name,"board_audio_tx"))return (void *)1;
 if(!strcmp(name,"taskLVGL"))return (void *)2;
 if(!strcmp(name,"IDLE0"))return (void *)3;
 return NULL;
}
static esp_err_t esp_register_freertos_tick_hook_for_cpu(void (*cb)(void),unsigned c){
 if((int)c==fail_core)return 7;
 assert(hooks[c]==NULL);hooks[c]=cb;return 0;
}
static void esp_deregister_freertos_tick_hook_for_cpu(void (*cb)(void),unsigned c){
 assert(hooks[c]==cb);hooks[c]=NULL;removed++;
 /* A late ISR already fetched the hook; disabled sampling must ignore it. */
 core=c;current=(void *)1;cb();
}
static void vTaskDelay(int ms){
 assert(ms==5000);delays++;
 for(unsigned i=0;i<5000;i++){
  core=0;current=i<1000?(void *)2:(void *)3;hooks[0]();
  core=1;current=i<3000?(void *)1:(void *)99;hooks[1]();
 }
 now+=5000000;
}
'''+declarations+function(s,"task_sample_tick")+function(s,"command_task_diag")+r'''
int main(void){char *args[]={"task-diag",NULL};
 assert(command_task_diag(1,args)==0);
 assert(strstr(output,"core=0 task=taskLVGL samples=1000 share-permille=200")!=NULL);
 assert(strstr(output,"core=0 task=IDLE0 samples=4000 share-permille=800")!=NULL);
 assert(strstr(output,"core=1 task=board_audio_tx samples=3000 share-permille=600")!=NULL);
 assert(strstr(output,"core=1 task=other samples=2000 share-permille=400")!=NULL);
 assert(strstr(output,"task=board_audio_rx unavailable")!=NULL);
 assert(delays==1 && removed==2 && hooks[0]==NULL && hooks[1]==NULL);
 assert(!s_task_sampling && s_task_samples[1][0]==3000);
 output[0]=0;assert(command_task_diag(1,args)==0);assert(s_task_samples[1][0]==3000);
 output[0]=0;unsigned old_delays=delays;fail_core=1;
 assert(command_task_diag(1,args)!=0);assert(delays==old_delays && !s_task_sampling);
 assert(hooks[0]==NULL && hooks[1]==NULL);
 assert(strstr(output,"hook registration failed")!=NULL);
 assert(command_task_diag(2,args)!=0);assert(delays==old_delays);
}
''')

    def test_other_targets_need_no_isr_or_freertos_trace(self):
        run_c(PREAMBLE+function(SOURCE.read_text(),"command_task_diag")+r'''
int main(void){char *args[]={"task-diag",NULL};assert(command_task_diag(1,args)!=0);
 assert(strstr(output,"not available")!=NULL);}
''')

if __name__ == "__main__": unittest.main()
