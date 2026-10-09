"""Exercise the actual SDK error callback after a local disconnect request."""
import unittest

from test_s3_product_regressions import ROOT, function, run_c


class BKCloseErrorCallback(unittest.TestCase):
    def test_closing_error_preserves_local_exit_and_remote_error_still_dispatches(self):
        source = (ROOT / 'firmware/beken/bk7258/lckfb-bk7258-xiaotai/ap/src/xiaotai_tirtc.c').read_text()
        run_c(r'''
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdatomic.h>
#define BK_LOGW(...) ((void)0)
typedef void *tirtc_conn_t;
static atomic_int s_connection_error;
static atomic_bool s_active_disconnect_queued;
static unsigned requests;
static bool connection_matches(tirtc_conn_t c) { return c == (void *)1; }
static unsigned xiaotai_tirtc_generation(void) { return 7; }
static bool queue_active_disconnect_once(tirtc_conn_t c, unsigned gen, int error) {
    assert(c == (void *)1 && gen == 7 && error == -40008);
    requests++;
    return true;
}
''' + function(source, 'on_conn_error') + r'''
int main(void) {
    /* Local teardown already owns the disconnect. The SDK emits its generic
     * close error after TiRtcDisconnect returns, before on_disconnected. */
    atomic_store(&s_active_disconnect_queued, true);
    on_conn_error((void *)1, -40008);
    assert(atomic_load(&s_connection_error) == 0);
    assert(requests == 0);
    /* An unexpected remote failure must still reach the media worker. */
    atomic_store(&s_active_disconnect_queued, false);
    on_conn_error((void *)1, -40008);
    assert(atomic_load(&s_connection_error) == -40008 && requests == 1);
    /* Late callbacks from an old handle cannot change the active result. */
    atomic_store(&s_connection_error, 0);
    on_conn_error((void *)2, -40008);
    assert(atomic_load(&s_connection_error) == 0 && requests == 1);
    /* Preserve the original failure if teardown is already in progress. */
    atomic_store(&s_active_disconnect_queued, true);
    atomic_store(&s_connection_error, -123);
    on_conn_error((void *)1, -40008);
    assert(atomic_load(&s_connection_error) == -123 && requests == 1);
    return 0;
}
''')


if __name__ == '__main__':
    unittest.main()
