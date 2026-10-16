"""Exercise the BK SDK's public heartbeat initialization with PSRAM stacks enabled."""

import os
import subprocess
import tempfile
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
SDK_SOURCE = (Path(os.environ.get('SDK_DIR',
              ROOT / '.tools/bk_avdk_smp-release-v3.1.1.10')) /
              'ap/middleware/driver/mailbox/mb_ipc_heartbeat.c')


class BKHeartbeatSramTest(unittest.TestCase):
    def test_heartbeat_uses_internal_sram_stack(self):
        if not SDK_SOURCE.is_file():
            self.skipTest('BK SDK checkout is required for the SDK interface test')
        with tempfile.TemporaryDirectory() as directory:
            temporary = Path(directory)
            (temporary / 'os').mkdir()
            (temporary / 'components').mkdir()
            (temporary / 'os/os.h').write_text('''
#include <stdint.h>
typedef uint32_t u32;
typedef int bk_err_t;
typedef void (*beken_thread_function_t)(void *);
#define BK_OK 0
#define BK_FAIL -1
void rtos_delay_milliseconds(unsigned int delay);
u32 rtos_get_time(void);
bk_err_t rtos_smp_create_thread(void *thread, unsigned char priority,
    const char *name, beken_thread_function_t function, u32 stack, void *arg);
bk_err_t rtos_create_sram_thread(void *thread, unsigned char priority,
    const char *name, beken_thread_function_t function, u32 stack, void *arg);
''')
            (temporary / 'components/log.h').write_text('''
#define BK_LOG_INFO 3
#define BK_LOGE(...) ((void)0)
''')
            (temporary / 'components/system.h').write_text('''
void bk_printf_raw_nonblock(int level, char *tag, const char *fmt, ...);
''')
            (temporary / 'mb_ipc_cmd.h').write_text('''
int ipc_send_power_up(void);
int ipc_send_heart_beat(unsigned int destination);
''')
            (temporary / 'heartbeat.c').write_bytes(SDK_SOURCE.read_bytes())
            (temporary / 'main.c').write_text('''
#include <string.h>
#include "os/os.h"
static int sram_calls;
static int psram_calls;
static unsigned int stack_bytes;
static unsigned char task_priority;
static char task_name[32];
bk_err_t rtos_create_sram_thread(void *thread, unsigned char priority,
    const char *name, beken_thread_function_t function, u32 stack, void *arg)
{
    (void)thread; (void)arg;
    if (!function) return BK_FAIL;
    sram_calls++;
    task_priority = priority;
    stack_bytes = stack;
    strncpy(task_name, name, sizeof(task_name) - 1);
    return BK_OK;
}
bk_err_t rtos_smp_create_thread(void *thread, unsigned char priority,
    const char *name, beken_thread_function_t function, u32 stack, void *arg)
{
    (void)thread; (void)priority; (void)name; (void)function;
    (void)stack; (void)arg;
    psram_calls++;
    return BK_OK;
}
void rtos_delay_milliseconds(unsigned int delay) { (void)delay; }
u32 rtos_get_time(void) { return 0; }
int ipc_send_power_up(void) { return 0; }
int ipc_send_heart_beat(unsigned int destination)
{ (void)destination; return 0; }
void bk_printf_raw_nonblock(int level, char *tag, const char *fmt, ...)
{ (void)level; (void)tag; (void)fmt; }
extern bk_err_t mb_ipc_heartbeat_init(void);
int main(void)
{
    if (mb_ipc_heartbeat_init() != BK_OK) return 1;
    if (sram_calls != 1 || psram_calls != 0) return 2;
    if (task_priority != 0 || strcmp(task_name, "heartbeat") != 0) return 3;
    if (stack_bytes < 1024) return 4;
    return 0;
}
''')
            executable = temporary / 'heartbeat-test'
            subprocess.run(['cc', '-std=c11', '-DCONFIG_CPU_CNT=2',
                            '-DCONFIG_TASK_STACK_IN_PSRAM=1',
                            '-DCONFIG_PSRAM_AS_SYS_MEMORY=1',
                            '-I', str(temporary), str(temporary / 'heartbeat.c'),
                            str(temporary / 'main.c'), '-o', str(executable)],
                           check=True, capture_output=True, text=True)
            result = subprocess.run([str(executable)], capture_output=True)
            self.assertEqual(0, result.returncode,
                             f'heartbeat init selected wrong stack; rc={result.returncode}')


if __name__ == '__main__':
    unittest.main()
