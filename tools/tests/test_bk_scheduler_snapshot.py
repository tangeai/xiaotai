"""Run the SDK event-wait entry with probes at each potentially blocking call."""
from pathlib import Path
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]
SDK = ROOT / '.tools/bk_avdk_smp-release-v3.1.1.10/ap/components/os_source/freertos_smp_v2p0/FreeRTOS-Kernel'


def function(text, name):
    start = text.rfind('\n', 0, text.index(name + '(')) + 1
    end = text.index('{', start) + 1
    depth = 1
    while depth:
        depth += (text[end] == '{') - (text[end] == '}')
        end += 1
    return text[start:end]


class SchedulerSnapshot(unittest.TestCase):
    def test_event_wait_identifies_lock_event_and_delay_boundaries(self):
        if not SDK.exists():
            self.skipTest('pinned Beken SDK not installed')
        body = function((SDK / 'tasks.c').read_text(), 'vTaskPlaceOnEventList')
        harness = r'''
#include <assert.h>
#include <stdint.h>
#include <stddef.h>
#define CONFIG_CRITICAL_LR_RECORD 1
#define configASSERT assert
#define pdTRUE 1
#define portGET_CORE_ID() core
#define prvENTER_CRITICAL(m) enter(m)
#define prvEXIT_CRITICAL(m) leave(m)
typedef uint32_t TickType_t;
typedef int BaseType_t;
typedef struct { uint32_t xItemValue; void *pxContainer; } ListItem_t;
typedef struct { uint32_t uxNumberOfItems; } List_t;
typedef struct { ListItem_t xEventListItem, xStateListItem; } TCB_t;
static unsigned core;
static unsigned xKernelLock;
static TCB_t task[2], *pxCurrentTCBs[2] = { &task[0], &task[1] };
static List_t delayed, *pxDelayedTaskList = &delayed;
volatile uint32_t debug_event_wait[2][8], debug_event_fault[2][16];
static unsigned xTickCount, uxSchedulerSuspended[2];
BaseType_t port_uxCriticalNesting[2], port_uxOldInterruptState[2];
#define __get_PRIMASK() 1U
#define __get_IPSR() 0U
#define __get_BASEPRI() 0U
#define __get_FAULTMASK() 0U
#define portNVIC_INT_CTRL_REG 0U
static unsigned calls;
static void enter(void *m) {
    assert(m == &xKernelLock);
    assert(debug_event_wait[core][0] == 1); calls++;
}
static void vListInsert(List_t *list, ListItem_t *item) {
    assert(debug_event_wait[core][0] == 2);
    assert(debug_event_wait[core][1] == (uint32_t)(uintptr_t)list);
    assert(item == &task[core].xEventListItem); calls++;
}
static void prvAddCurrentTaskToDelayedList(TickType_t wait, int forever) {
    assert(debug_event_wait[core][0] == 3);
    assert(debug_event_wait[core][5] == wait && forever == 1); calls++;
}
static void leave(void *m) {
    assert(m == &xKernelLock && debug_event_wait[core][0] == 4); calls++;
}
''' + body + r'''
int main(void) {
    List_t event = {0};
    for (core=0; core<2; core++) {
        vTaskPlaceOnEventList(&event, 100 + core);
        assert(debug_event_wait[core][0] == 0);
    }
    assert(calls == 8);
}
'''
        with tempfile.TemporaryDirectory() as tmp:
            tmp = Path(tmp)
            (tmp/'test.c').write_text(harness)
            subprocess.run(['cc', '-Wno-pointer-to-int-cast', str(tmp/'test.c'), '-o', str(tmp/'test')], check=True, capture_output=True)
            run = subprocess.run([str(tmp/'test')], capture_output=True, text=True, timeout=3)
            self.assertEqual(run.returncode, 0, run.stderr)


    def test_port_probe_preserves_nested_interrupt_and_lock_state(self):
        if not SDK.exists():
            self.skipTest('pinned Beken SDK not installed')
        source = (SDK / 'portable/GCC/ARM_CM33_NTZ/non_secure/port.c').read_text()
        harness = r'''
#include <assert.h>
#include <stdint.h>
#include <stddef.h>
#define CONFIG_CRITICAL_LR_RECORD 1
#define pdFAIL 0
#define pdPASS 1
#define portGET_CORE_ID() core
#define __get_LR() 0x1235
#define portSET_INTERRUPT_MASK_FROM_ISR() mask()
#define portCLEAR_INTERRUPT_MASK_FROM_ISR(x) (irq = (x))
typedef int BaseType_t;
typedef struct { uint32_t owner, count; } portMUX_TYPE;
static unsigned core, irq;
static BaseType_t port_uxCriticalNesting[2], port_uxOldInterruptState[2];
volatile uint32_t debug_port_wait[2][8];
static unsigned depth[2];
static unsigned mask(void) { unsigned old = irq; irq = 1; return old; }
static int spinlock_acquire(portMUX_TYPE *m, int timeout) {
    (void)timeout;
    assert(irq == 1 && debug_port_wait[core][0] == 1);
    assert(debug_port_wait[core][1] == (uint32_t)(uintptr_t)m);
    m->owner=core; m->count++; return 1;
}
static void spinlock_release(portMUX_TYPE *m, int old) {
    (void)old;
    assert(irq == 1 && debug_port_wait[core][0] == 5);
    assert(m->owner == core && m->count); m->count--;
}
static void check_critical_crc(void) {
    assert(debug_port_wait[core][0] == 2 || debug_port_wait[core][0] == 6);
}
static void update_critical_crc(void) {}
static void vPortEnterCriticalLrRecord(unsigned lr) { (void)lr; depth[core]++; }
static void vPortExitCriticalLrRecord(unsigned lr) { (void)lr; depth[core]--; }
''' + function(source, 'xPortEnterCriticalTimeout') + '\n' + function(source, 'vPortExitCritical') + r'''
int main(void) {
    for (core=0; core<2; core++) {
        portMUX_TYPE first={0}, second={0}; irq=0;
        assert(xPortEnterCriticalTimeout(&first,-1) == 1);
        assert(irq == 1 && port_uxCriticalNesting[core] == 1);
        assert(xPortEnterCriticalTimeout(&second,-1) == 1);
        assert(depth[core] == 2 && debug_port_wait[core][7] == 2);
        vPortExitCritical(&second);
        assert(irq == 1 && port_uxCriticalNesting[core] == 1);
        vPortExitCritical(&first);
        assert(irq == 0 && depth[core] == 0 && debug_port_wait[core][0] == 0);
        assert(first.count == 0 && second.count == 0);
    }
}
'''
        with tempfile.TemporaryDirectory() as tmp:
            tmp = Path(tmp)
            (tmp/'port.c').write_text(harness)
            subprocess.run(['cc', '-Wno-pointer-to-int-cast', str(tmp/'port.c'), '-o', str(tmp/'port')], check=True, capture_output=True)
            run = subprocess.run([str(tmp/'port')], capture_output=True, text=True, timeout=3)
            self.assertEqual(run.returncode, 0, run.stderr)


if __name__ == '__main__':
    unittest.main()
