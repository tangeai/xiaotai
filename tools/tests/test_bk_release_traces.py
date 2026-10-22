"""Release heartbeat keeps sending while routine debug probes stay silent."""
from pathlib import Path
import subprocess
import tempfile
import unittest
from test_s3_product_regressions import ROOT, function, run_c

PROJECT = ROOT / 'firmware/beken/bk7258/lckfb-bk7258-xiaotai'

class BKReleaseTraces(unittest.TestCase):
    def test_heartbeat_profiles_preserve_delivery(self):
        fixture = ROOT / 'tools/tests/fixtures/bk_heartbeat/mb_ipc_task.c'
        patch = ROOT / 'platforms/beken/tools/patches/bk-avdk-heartbeat-debug-level.patch'
        with tempfile.TemporaryDirectory() as tmp:
            path = Path(tmp) / 'ap/middleware/driver/mailbox/mb_ipc_heartbeat.c'
            path.parent.mkdir(parents=True)
            path.write_text(fixture.read_text())
            if patch.exists():
                subprocess.run(['git', 'apply', '--unsafe-paths', '--directory='+tmp,
                                str(patch)], check=True, capture_output=True)
            task = function(path.read_text(), 'mb_ipc_task')
        for level in (3, 4):
            run_c(f'#define CONFIG_LOG_LEVEL {level}\n' + r'''
#include <assert.h>
#include <setjmp.h>
typedef unsigned u32;
typedef int bk_err_t;
#define BK_LOG_INFO 3
#define MOD_TAG "hrt"
#define MB_IPC_HEARTBEAT_TIME 2000
static jmp_buf done;
static unsigned sends,logs,waits,powerups;
static void ipc_send_power_up(void){powerups++;}
static int ipc_send_heart_beat(unsigned cpu){assert(cpu==0);sends++;return 0;}
static unsigned rtos_get_time(void){return waits*2000;}
static void rtos_delay_milliseconds(unsigned ms){assert(ms==2000);if(++waits==4)longjmp(done,1);}
static void bk_printf_raw_nonblock(int level,const char *tag,const char *fmt,...){logs++;}
''' + task + r'''
int main(void){if(!setjmp(done))mb_ipc_task(0);
 assert(sends==3 && powerups==1);
 assert(logs==(CONFIG_LOG_LEVEL>3 ? 6U:0U));}
''')

    def test_metrics_preserves_info_sample_without_release_trace(self):
        source = (ROOT / 'platforms/beken/runtime/src/xiaotai_metrics.c').read_text()
        for level in (3, 4):
            run_c(f'#define CONFIG_LOG_LEVEL {level}\n' + r'''
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdatomic.h>
#include <stdio.h>
#define BK_LOG_NONE 0
#define BK_LOG_INFO 3
#define BK_OK 0
#define BK_FAIL -1
#define TAG "metrics"
#define configMAX_TASK_NAME_LEN 16
static unsigned traces,samples;
static int s_state_mutex;
static char s_state[24]="READY";
static atomic_int s_rssi=-128;
static atomic_uint s_sta_connects=1,s_sta_disconnects;
static struct {bool sampled;unsigned cpu_percent,task_count,stack_low_words;char stack_low_task[17];} s_snapshot;
static void rtos_lock_mutex(int *m){}
static void rtos_unlock_mutex(int *m){}
static unsigned uxTaskGetNumberOfTasks(void){return 28;}
static unsigned rtos_get_time(void){return 10000;}
static unsigned rtos_get_free_heap_size(void){return 57304;}
static unsigned rtos_get_minimum_free_heap_size(void){return 56672;}
static unsigned rtos_get_psram_free_heap_size(void){return 10673000;}
static unsigned rtos_get_psram_minimum_free_heap_size(void){return 10663000;}
static void bk_printf_raw_nonblock(int level,void *tag,const char *fmt,...){traces++;}
static void bk_printf_nonblock(int level,const char *tag,const char *fmt,...){assert(level==3);samples++;}
''' + function(source, 'emit_snapshot') + r'''
int main(void){emit_snapshot();assert(samples==1 && s_snapshot.task_count==28);
 assert(traces==(CONFIG_LOG_LEVEL>3 ? 3U:0U));}
''')

if __name__ == '__main__': unittest.main()
