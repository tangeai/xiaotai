"""Keep UI rendering off the synchronous AP-to-CP Wi-Fi status RPC."""

from pathlib import Path
import unittest
from test_s3_product_regressions import function, run_c


ROOT = Path(__file__).resolve().parents[2]
AP = ROOT / "platforms/beken/runtime"


class TestBkRssiCache(unittest.TestCase):
    def test_ui_uses_cached_rssi(self):
        app = (AP / "src/xiaotai_app.c").read_text()
        self.assertNotIn("bk_wifi_sta_get_link_status", app)
        self.assertGreaterEqual(app.count("xiaotai_metrics_cached_rssi()"), 2)

    def test_cache_resets_when_link_drops(self):
        metrics = (AP / "src/xiaotai_metrics.c").read_text()
        self.assertIn("atomic_store_explicit(&s_rssi, -128", metrics)

    def test_periodic_metrics_never_uses_synchronous_wifi_rpc(self):
        source = (AP / "src/xiaotai_metrics.c").read_text()
        run_c(r'''
#include <assert.h>
#include <setjmp.h>
#include <stdatomic.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#define BK_OK 0
#define BK_FAIL -1
#define BK_ERR_BUSY -2
#define BK_LOG_NONE 0
#define BK_LOG_INFO 3
#define TAG "xiaotai_metrics"
#define METRICS_INTERVAL_MS 10000U
#define configMAX_TASK_NAME_LEN 16
#define LWIP_STATS 0
#define LINK_STATS 0
static unsigned blocking_log_calls, nonblocking_metrics_calls;
static void BK_LOGI(const char *tag, const char *fmt, ...)
{
    (void)tag;
    (void)fmt;
    blocking_log_calls++;
}
static void bk_printf_nonblock(int level, char *tag, const char *fmt, ...)
{
    assert(level == BK_LOG_INFO);
    assert(strcmp(tag, "xiaotai_metrics") == 0);
    assert(strncmp(fmt, "METRICS ", 8) == 0);
    nonblocking_metrics_calls++;
}
typedef void *beken_thread_t;
typedef void *beken_mutex_t;
typedef void *beken_thread_arg_t;
typedef struct { int rssi; } wifi_link_status_t;
typedef struct {
    bool sampled;
    unsigned cpu_percent, task_count, stack_low_words;
    char stack_low_task[17];
} xiaotai_metrics_snapshot_t;
static beken_thread_t s_thread;
static beken_mutex_t s_state_mutex;
static bool s_state_mutex_ready;
static char s_state[24] = "READY";
static xiaotai_metrics_snapshot_t s_snapshot;
static atomic_uint s_sta_connects, s_sta_disconnects;
static atomic_int s_rssi = ATOMIC_VAR_INIT(-128);
static unsigned wifi_rpc_calls;
static jmp_buf stop_after_sample;
static int bk_wifi_sta_get_link_status(wifi_link_status_t *link)
{
    wifi_rpc_calls++;
    link->rssi = -40;
    return BK_OK;
}
static void bk_printf_raw_nonblock(int level, void *tag, const char *fmt, ...) {}
static void rtos_lock_mutex(beken_mutex_t *mutex) {}
static void rtos_unlock_mutex(beken_mutex_t *mutex) {}
static int rtos_init_mutex(beken_mutex_t *mutex) { *mutex = (void *)1; return BK_OK; }
static unsigned uxTaskGetNumberOfTasks(void) { return 23; }
static unsigned rtos_get_time(void) { return 10000; }
static unsigned rtos_get_free_heap_size(void) { return 111000; }
static unsigned rtos_get_minimum_free_heap_size(void) { return 108000; }
static unsigned rtos_get_psram_free_heap_size(void) { return 10955000; }
static unsigned rtos_get_psram_minimum_free_heap_size(void) { return 10948000; }
static void rtos_delay_milliseconds(unsigned ms) { longjmp(stop_after_sample, 1); }
static int rtos_create_psram_thread(beken_thread_t *thread, int priority,
                                    const char *name,
                                    void (*entry)(beken_thread_arg_t),
                                    unsigned stack, void *arg)
{
    *thread = (void *)1;
    entry(arg);
    return BK_OK;
}
''' + function(source, 'emit_snapshot') +
              function(source, 'metrics_task') +
              function(source, 'xiaotai_metrics_start') +
              function(source, 'xiaotai_metrics_cached_rssi') + r'''
int main(void)
{
    if (setjmp(stop_after_sample) == 0) (void)xiaotai_metrics_start();
    assert(s_thread != NULL);
    assert(wifi_rpc_calls == 0);
    assert(xiaotai_metrics_cached_rssi() == -128);
    assert(blocking_log_calls == 0);
    assert(nonblocking_metrics_calls == 1);
    return 0;
}
''')

    def test_network_page_separates_connection_from_unknown_rssi(self):
        source = (AP / "src/xiaotai_ui.c").read_text()
        run_c(r'''
#include <assert.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
typedef int frame_buffer_t;
static frame_buffer_t frame;
static char status[64], strength[64];
static frame_buffer_t *page_frame(const char *title) { return &frame; }
static void centered_mixed_text(frame_buffer_t *f, int y, const char *value,
                                int scale, int color)
{
    if (y == 58) snprintf(status, sizeof(status), "%s", value);
    if (y == 112) snprintf(strength, sizeof(strength), "%s", value);
}
static void flush_page(frame_buffer_t *f) {}
''' + function(source, 'xiaotai_ui_show_network') + r'''
int main(void)
{
    xiaotai_ui_show_network(-128, "192.168.3.13");
    assert(strcmp(status, "WiFi已连接") == 0);
    assert(strcmp(strength, "信号强度未知") == 0);
    xiaotai_ui_show_network(-128, NULL);
    assert(strcmp(status, "网络未连接") == 0);
    xiaotai_ui_show_network(-42, "192.168.3.13");
    assert(strcmp(strength, "信号强度 -42") == 0);
    return 0;
}
''')

    def test_network_page_does_not_show_stale_ip_after_disconnect(self):
        source = (AP / "src/xiaotai_app.c").read_text()
        run_c(r'''
#include <assert.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>
static bool ready;
static const char *shown_ip;
static bool xiaotai_network_ready(void) { return ready; }
static int xiaotai_metrics_cached_rssi(void) { return -128; }
static int xiaotai_network_get_ip(char *address, size_t capacity)
{
    snprintf(address, capacity, "192.168.3.13");
    return 0;
}
static void xiaotai_ui_show_network(int rssi, const char *ip)
{
    shown_ip = ip;
}
''' + function(source, 'render_network') + r'''
int main(void)
{
    ready = false;
    render_network();
    assert(shown_ip == NULL);
    ready = true;
    render_network();
    assert(shown_ip != NULL);
    return 0;
}
''')


if __name__ == "__main__":
    unittest.main()
