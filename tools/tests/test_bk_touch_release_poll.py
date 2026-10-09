"""Replay the patched SDK task with a missing release interrupt."""
from pathlib import Path
import subprocess
import tempfile
import unittest
from test_s3_product_regressions import ROOT, function, run_c

PATCH = ROOT / 'firmware/beken/bk7258/lckfb-bk7258-xiaotai/tools/patches/bk-avdk-touch-active-release-poll.patch'
FIXTURE = ROOT / 'tools/tests/fixtures/bk_touch/tp_process_task.c'
SDK_FILE = 'ap/middleware/driver/tp/tp_driver.c'

class BkTouchReleasePoll(unittest.TestCase):
    def test_missing_irq_reads_actual_release_then_returns_to_idle_wait(self):
        with tempfile.TemporaryDirectory() as directory:
            sdk = Path(directory)
            source = sdk / SDK_FILE
            source.parent.mkdir(parents=True)
            source.write_text(FIXTURE.read_text())
            if PATCH.exists():
                subprocess.run(['git', 'apply', '--unsafe-paths', '--directory='+str(sdk), str(PATCH)], check=True, capture_output=True)
            task = function(source.read_text(), 'tp_process_task')
        run_c(r'''
#include <assert.h>
#include <setjmp.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#define BK_OK 0
#define BK_FAIL -1
#define kNoErr 0
#define BEKEN_NEVER_TIMEOUT 0xffffffffU
#define TP_SUPPORT_MAX_NUM 1
#define TP_READ_RETRY_COUNT 3
#define TP_READ_RETRY_DELAY_MS 2
#define TP_RECOVERY_FAILURE_COUNT 3
#define TP_EVENT_TYPE_DOWN 1
#define TP_EVENT_TYPE_MOVE 2
#define TP_EVENT_TYPE_UP 3
#define LOGE(...) printf(__VA_ARGS__)
#define LOGW(...) printf(__VA_ARGS__)
#define LOGI(...) printf(__VA_ARGS__)
#define LOGV(...) ((void)0)
#define BK_LOG_ON_ERR(...) ((void)0)
#define os_memset memset
typedef unsigned beken_thread_arg_t;
typedef struct {int event,track_id,x_coordinate,y_coordinate,width;unsigned timestamp;} tp_data_t;
static jmp_buf done;
static int tp_sema,tp_i2c_cb,waits,reads,down,up;
static int rtos_get_semaphore(int *sem,unsigned timeout){
 waits++;
 if(waits==1){assert(timeout==BEKEN_NEVER_TIMEOUT);return BK_OK;}
 if(waits==2 || waits==3){assert(timeout==20);return BK_FAIL;} /* Hold, then lift without another IRQ. */
 assert(timeout==BEKEN_NEVER_TIMEOUT);longjmp(done,1);return 0;
}
static int read_info(int *bus,unsigned count,void *data){
 tp_data_t *p=data;reads++;
 p->event=reads==1 ? TP_EVENT_TYPE_DOWN : reads==2 ? TP_EVENT_TYPE_MOVE : TP_EVENT_TYPE_UP;return BK_OK;
}
static struct {int (*read_tp_info)(int*,unsigned,void*);} sensor={read_info},*current_sensor=&sensor;
static void rtos_delay_milliseconds(unsigned t){}
static int tp_recover_sensor(void){return BK_OK;}
static void bk_tp_read_info_callback(tp_data_t *p){if(p->event==TP_EVENT_TYPE_UP)up++;else down++;}
''' + task + r'''
int main(void){if(setjmp(done)==0)tp_process_task(0);assert(reads==3 && down==2 && up==1);}
''')

if __name__ == '__main__':
    unittest.main()
