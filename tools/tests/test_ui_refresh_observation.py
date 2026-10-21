"""Replay UI timing boundaries and the LVGL monitor with deterministic clocks.

This verifies the probes, not hardware throughput or the cause of an overrun.
"""
import unittest
from test_s3_product_regressions import ROOT, function, run_c

PRODUCT = ROOT / "platforms/esp-idf/components/starter_product/src/starter_product.c"

PREAMBLE = r'''
#include <assert.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdarg.h>
#include <string.h>
static int64_t clock_us;
static int64_t esp_timer_get_time(void){return clock_us;}
static char output[2048];
static unsigned logs;
static void capture(const char *fmt, ...){
 va_list ap;va_start(ap,fmt);vsnprintf(output,sizeof(output),fmt,ap);va_end(ap);logs++;
}
#define TAG "ui-test"
#define ESP_LOGW(tag,...) capture(__VA_ARGS__)
#define ESP_LOGI(tag,...) capture(__VA_ARGS__)
#define PRODUCT_UI_REFRESH_SLOW_US 20000LL
#define PRODUCT_UI_REFRESH_LOG_INTERVAL_US 5000000LL
'''


class UiRefreshObservation(unittest.TestCase):
    def test_tick_distinguishes_snapshot_wait_from_view_work(self):
        body = function(PRODUCT.read_text(), "product_tick")
        # Real tick prefix: SDK fakes inject elapsed time at public readers.
        prefix = body[:body.index("    bool runtime_changed")]
        tail = body[body.index("    const int64_t refresh_elapsed_us"):]
        run_c(PREAMBLE + r'''
typedef int lv_timer_t;
typedef int starter_voice_result_t;
typedef struct {int state;} starter_runtime_status_t;
typedef int starter_runtime_product_snapshot_t;
typedef int starter_media_status_t;
static int64_t s_ui_refresh_slow_last_us;
static char s_voice_feedback[2];
static int s_voice_feedback_hide_ms, s_wifi_notice, s_page;
static void *s_voice_queue;
#define WIFI_NOTICE_NONE 0
#define PRODUCT_VOICE_QUEUE_LENGTH 8
#define pdTRUE 1
static int64_t monotonic_ms(void){return clock_us/1000;}
static int xQueueReceive(void *q,void *v,int wait){clock_us+=100;return 0;}
static void handle_voice_result(int *r,int64_t now){}
static starter_runtime_status_t starter_runtime_status_snapshot(void){
 clock_us+=200;return (starter_runtime_status_t){3};
}
static starter_runtime_product_snapshot_t starter_runtime_product_snapshot(void){clock_us+=25000;return 0;}
static starter_media_status_t starter_media_status(void){clock_us+=300;return 0;}
''' + prefix + r'''
    (void)voice_state_changed;(void)product;(void)media;
    /* Remaining view work, including expression, injected independently. */
    clock_us += 5400; expression_elapsed_us = 1000;
''' + tail + r'''
int main(void){
 clock_us=6000000;product_tick(NULL);
 assert(logs==1);
 assert(strstr(output,"total=31000 us")!=NULL);
 assert(strstr(output,"queue=100 runtime=200 product=25000 media=300 view=4400")!=NULL);
 assert(strstr(output,"expression=1000 us")!=NULL);
 product_tick(NULL);assert(logs==1); /* keep the existing 5-second log budget */
}
''')

    def test_render_monitor_counts_actual_pixels_and_keeps_previous_callback(self):
        source = PRODUCT.read_text()
        run_c(PREAMBLE + r'''
typedef struct drv drv;
struct drv {void (*monitor_cb)(drv *,uint32_t,uint32_t);};
typedef drv lv_disp_drv_t;
typedef struct {drv *driver;} lv_disp_t;
static unsigned forwarded;
static void previous(drv *d,uint32_t ms,uint32_t px){forwarded++;}
static void (*s_previous_render_monitor)(lv_disp_drv_t *,uint32_t,uint32_t);
''' + function(source, "product_render_monitor") +
            function(source, "product_render_probe_install") + r'''
int main(void){
 drv driver={previous};lv_disp_t display={&driver};
 clock_us=1000000;product_render_probe_install(&display);
 /* Reinstall must not chain the callback to itself. */
 product_render_probe_install(&display);
 driver.monitor_cb(&driver,4,2911);assert(logs==0 && forwarded==1);
 clock_us+=1000000;driver.monitor_cb(&driver,7,2911);
 clock_us+=4000000;driver.monitor_cb(&driver,30,76800);
 assert(logs==1 && forwarded==3);
 assert(strstr(output,"refreshes=3 pixels=82622 max-pixels=76800")!=NULL);
 assert(strstr(output,"render-ms=41 max-render-ms=30")!=NULL);
 clock_us+=5000000;driver.monitor_cb(&driver,3,2911);
 assert(logs==2 && forwarded==4);
 assert(strstr(output,"refreshes=1 pixels=2911 max-pixels=2911")!=NULL);
 assert(strstr(output,"render-ms=3 max-render-ms=3")!=NULL);
}
''')


if __name__ == "__main__":
    unittest.main()
