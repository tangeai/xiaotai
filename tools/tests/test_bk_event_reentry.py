"""Replay the build.47 duplicate event insertion through the actual SDK code.

This tests first-fault capture, not a cure for the hardware scheduling failure.
"""
from pathlib import Path
import subprocess
import tempfile
import unittest

from test_bk_scheduler_snapshot import SDK, function


class EventReentry(unittest.TestCase):
    def test_queue_boundary_probe_preserves_mask_and_freezes_first_fault(self):
        if not SDK.exists():
            self.skipTest('pinned Beken SDK not installed')
        port = (SDK/'portable/GCC/ARM_CM33_NTZ/non_secure/port.c').read_text()
        harness = r'''
#include <stdint.h>
#include <assert.h>
#define portGET_CORE_ID() get_core()
#define portSET_INTERRUPT_MASK_FROM_ISR() mask()
#define portCLEAR_INTERRUPT_MASK_FROM_ISR(x) restore(x)
#define __get_IPSR() 7U
#define __get_BASEPRI() 128U
#define __get_FAULTMASK() 0U
#define portNVIC_INT_CTRL_REG 0x10000000U
static uint32_t core, irq;
static uint32_t get_core(void) { return core; }
static int port_uxCriticalNesting[2]={2,2}, port_uxOldInterruptState[2];
volatile uint32_t debug_event_fault[2][16], debug_queue_wait[2][3][8];
static uint32_t mask(void) { uint32_t old=irq; irq=1; return old; }
static void restore(uint32_t value) { irq=value; }
''' + function(port, 'vPortDebugQueueWait') + r'''
int main(void) {
    for(core=0;core<2;core++) {
        for(unsigned stage=0;stage<3;stage++) {
            irq=stage==1;
            vPortDebugQueueWait(0x1234,stage);
            assert(irq==(stage==1));
            assert(debug_queue_wait[core][stage][0]==0x1234);
            assert(debug_queue_wait[core][stage][1]==irq);
            assert(debug_queue_wait[core][stage][2]==7);
            assert(debug_queue_wait[core][stage][3]==128);
            assert(debug_queue_wait[core][stage][5]==0x10000000);
            assert(debug_queue_wait[core][stage][6]==2);
        }
        debug_event_fault[core][0]=1;
        vPortDebugQueueWait(0x5678,2);
        assert(debug_queue_wait[core][2][0]==0x1234 && irq==0);
    }
}
'''
        with tempfile.TemporaryDirectory() as directory:
            directory=Path(directory)
            (directory/'probe.c').write_text(harness)
            subprocess.run(['cc', str(directory/'probe.c'), '-o', str(directory/'probe')],
                           check=True, capture_output=True)
            run=subprocess.run([str(directory/'probe')], capture_output=True, timeout=3)
            self.assertEqual(run.returncode,0,run.stderr)

    def test_first_duplicate_is_captured_before_the_list_becomes_a_cycle(self):
        if not SDK.exists():
            self.skipTest('pinned Beken SDK not installed')
        tasks = (SDK / 'tasks.c').read_text()
        lists = (SDK / 'list.c').read_text()
        harness = r'''
#include <assert.h>
#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
#define CONFIG_CRITICAL_LR_RECORD 1
#define configASSERT assert
#define pdTRUE 1
#define portMAX_DELAY UINT32_MAX
#define portGET_CORE_ID() core
#define prvENTER_CRITICAL(m) ((void)(m))
#define prvEXIT_CRITICAL(m) ((void)(m))
#define listTEST_LIST_INTEGRITY(x) ((void)(x))
#define listTEST_LIST_ITEM_INTEGRITY(x) ((void)(x))
#define __get_PRIMASK() 1U
#define __get_BASEPRI() 0U
#define __get_IPSR() 0U
#define __get_FAULTMASK() 0U
#define portNVIC_INT_CTRL_REG 0x10000000U
typedef uint32_t TickType_t;
typedef struct List List_t;
typedef struct ListItem {
    uint32_t xItemValue;
    struct ListItem *pxNext, *pxPrevious;
    void *pvOwner;
    List_t *pxContainer;
} ListItem_t;
struct List { unsigned uxNumberOfItems; ListItem_t *pxIndex; ListItem_t xListEnd; };
typedef struct { ListItem_t xEventListItem, xStateListItem; } TCB_t;
static unsigned core, xKernelLock, xTickCount=3057;
static unsigned uxSchedulerSuspended[2];
static int port_uxCriticalNesting[2]={2,2}, port_uxOldInterruptState[2];
static TCB_t task[2], *pxCurrentTCBs[2]={&task[0],&task[1]};
static List_t delayed, *pxDelayedTaskList=&delayed;
volatile uint32_t debug_event_wait[2][8], debug_event_fault[2][16];
static void prvAddCurrentTaskToDelayedList(unsigned wait, int forever) {
    (void)forever;
    task[core].xStateListItem.xItemValue=xTickCount+wait;
    task[core].xStateListItem.pxContainer=&delayed;
}
''' + function(lists, 'vListInsert') + '\n' + function(tasks, 'vTaskPlaceOnEventList') + r'''
int main(void) {
    for(core=0;core<2;core++) {
        List_t event={0};
        event.xListEnd.xItemValue=UINT32_MAX;
        event.xListEnd.pxNext=event.xListEnd.pxPrevious=&event.xListEnd;
        event.pxIndex=&event.xListEnd;
        ListItem_t *item=&task[core].xEventListItem;
        item->xItemValue=6; item->pvOwner=&task[core];
        vTaskPlaceOnEventList(&event,100);
        assert(event.uxNumberOfItems==1 && debug_event_fault[core][0]==0);
        /* The same still-blocked task enters again, as in the hardware dump. */
        vTaskPlaceOnEventList(&event,100);
        assert(debug_event_fault[core][0]==1 && "first duplicate was not captured");
        assert(debug_event_fault[core][3]==1); /* count BEFORE corruption */
        assert(debug_event_fault[core][4]==3057);
        assert(debug_event_fault[core][5]==3157);
        assert(debug_event_fault[core][8]==1 && debug_event_fault[core][9]==0);
        assert(debug_event_fault[core][12]==2);
        assert(item->pxNext==item && item->pxPrevious==item);
        assert(event.uxNumberOfItems==2);
        puts("PASS: real SDK duplicate insertion reproduces build.47 self-loop; first fault retained");
    }
}
'''
        with tempfile.TemporaryDirectory() as directory:
            directory = Path(directory)
            (directory/'replay.c').write_text(harness)
            built = subprocess.run(['cc', '-Wno-pointer-to-int-cast', str(directory/'replay.c'),
                                    '-o', str(directory/'replay')], capture_output=True, text=True)
            self.assertEqual(built.returncode, 0, built.stderr)
            run = subprocess.run([str(directory/'replay')], capture_output=True, text=True, timeout=3)
            self.assertEqual(run.returncode, 0, run.stderr)


if __name__ == '__main__':
    unittest.main()
