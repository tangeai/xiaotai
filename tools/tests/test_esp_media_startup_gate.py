"""Production startup gate: no resource user runs before commit; abort owns tasks."""
import re
import unittest
from test_s3_product_regressions import ROOT,run_c

class StartupGate(unittest.TestCase):
    def test_wait_commit_and_abort_order(self):
        header=(ROOT/'platforms/esp-idf/components/starter_media_common/include/starter_media_startup.h').read_text()
        header=re.sub(r'^#(?:include|pragma).*$', '',header,flags=re.M)
        run_c(r'''
#include <assert.h>
#include <stdbool.h>
#include <stddef.h>
#include <setjmp.h>
typedef void (*TaskFunction_t)(void*);
typedef void *TaskHandle_t;
#define pdTRUE 1
#define portMAX_DELAY 999
static jmp_buf blocked;
static unsigned notified,ran,deleted;
static unsigned ulTaskNotifyTake(int clear,unsigned wait){assert(clear==1&&wait==999);if(!notified)longjmp(blocked,1);notified--;return 1;}
static void xTaskNotifyGive(void*h){assert(h);notified++;}
static void vTaskDeleteWithCaps(void*h){deleted++;}
static void run(void*p){ran++;}
''' + header + r'''
int main(void){
 starter_media_worker_t workers[4]={{run,(void*)1},{run,(void*)2}};
 if(!setjmp(blocked)){starter_media_worker_entry(&workers[0]);assert(false);}
 assert(ran==0 && deleted==0);
 starter_media_workers_abort(workers,4);
 assert(deleted==2 && !workers[0].handle && !workers[1].handle);
 starter_media_workers_abort(workers,4);assert(deleted==2);
 for(unsigned i=0;i<4;i++)workers[i]=(starter_media_worker_t){run,(void*)(size_t)(i+1)};
 starter_media_workers_commit(workers,4);assert(notified==4);
 for(unsigned i=0;i<4;i++)starter_media_worker_entry(&workers[i]);
 assert(ran==4 && notified==0 && deleted==6);
}
''')
if __name__=='__main__':unittest.main()
