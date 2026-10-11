"""Exercise production SDIO credit polling while the duplex bus is busy."""
import unittest
from test_s3_product_regressions import function, run_c
from test_p4_hosted_tx import SOURCE

class SdioTxFairness(unittest.TestCase):
    def test_credit_wait_releases_bus_but_register_access_stays_serialized(self):
        run_c(r'''
#include <assert.h>
#include <stdint.h>
#include <stdbool.h>
#define MAX_WRITE_BUF_RETRIES 50
#define BUFFER_AVAILABLE 1
#define BUFFER_UNAVAILABLE 0
#define ESP_HOSTED_SDIO_UNRESPONSIVE_CODE -100
#define ACQUIRE_LOCK false
#define ESP_LOGE(...) ((void)0)
#define ESP_LOGV(...) ((void)0)
static bool locked=true;
static unsigned polls, sleeps, rx_progress, credit;
#define SDIO_DRV_LOCK() do {assert(!locked);locked=true;} while(0)
#define SDIO_DRV_UNLOCK() do {assert(locked);locked=false;} while(0)
static int sdio_get_tx_buffer_num(uint32_t *out,bool acquire) {
 assert(locked && !acquire);polls++;*out=credit;return 0;
}
static void wait_ms(int ms) {
 assert(ms==1);
 /* An RX task must be able to acquire the shared bus during a TX wait. */
 assert(!locked);rx_progress++;sleeps++;
 if(sleeps==30)credit=3;
}
static void restart(void) {assert(0);}
static struct {struct {void (*_h_msleep)(int);void (*_h_restart_host)(void);} *funcs;} g_h;
''' + function(SOURCE.read_text(), 'sdio_is_write_buffer_available') + r'''
int main(void) {
 static __typeof__(*g_h.funcs) funcs={wait_ms,restart};g_h.funcs=&funcs;
 /* Exhaustion keeps caller ownership of the lock and permits RX progress. */
 assert(sdio_is_write_buffer_available(1)==BUFFER_UNAVAILABLE);
 assert(locked && polls==50 && sleeps==25 && rx_progress==25);
 /* A later replenishment succeeds; cached credits are not double spent. */
 assert(sdio_is_write_buffer_available(1)==BUFFER_AVAILABLE);
 assert(locked && credit==3 && sleeps==30);
 unsigned before=polls;
 assert(sdio_is_write_buffer_available(2)==BUFFER_AVAILABLE);
 assert(polls==before && locked);
 credit=0;sleeps=100;
 assert(sdio_is_write_buffer_available(1)==BUFFER_UNAVAILABLE);
 assert(locked && polls>before);
 return 0;
}
''')

if __name__ == '__main__':
    unittest.main()
