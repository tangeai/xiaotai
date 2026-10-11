"""Replay actual worker scheduling boundaries with sustained decoded input.

This protects blocking opportunities, not the dual-core IDLE execution itself;
that requires a real FreeRTOS/decoder workload on the board.
"""
import re
import unittest
from test_s3_product_regressions import ROOT, run_c, function

SOURCE = ROOT / 'platforms/esp-idf/waveshare_p4/call_video_renderer.c'
CONFIG = ROOT / 'firmware/esp-idf/esp32p4/waveshare-touch-lcd-43c/main/services/call_video_renderer_config.h'

class VideoScheduling(unittest.TestCase):
    def test_busy_workers_block_even_with_input_backlog(self):
        source = SOURCE.read_text()
        config = '\n'.join(re.findall(r'^#define CALL_VIDEO_DECODE_(?:IDLE_WINDOW\w*|SCHEDULING_WINDOW_MS)\s+[^\n]+', CONFIG.read_text(), re.M))
        helper = function(source, 'call_video_worker_scheduling_window') if 'static void call_video_worker_scheduling_window(' in source else ''
        decoder = function(source, 'call_video_renderer_task')
        start = decoder.index('        call_video_return_slot(index);', decoder.index('bool slow_decode'))
        end = decoder.index('        if (generation_stale)', start)
        decoder = decoder[start:end]
        converter = function(source, 'call_video_converter_task')
        start = converter.rindex('        call_video_return_decoded_slot(index);')
        converter = converter[start:converter.index('\n    }', start)]
        run_c(r'''
#include <assert.h>
#include <stdint.h>
typedef unsigned TickType_t;
static int64_t now_us;
static unsigned blocks, long_blocks;
static void vTaskDelay(unsigned ticks) {assert(ticks>0);blocks++;if(ticks>=5)long_blocks++;now_us+=ticks*1000;}
static int64_t esp_timer_get_time(void) {return now_us;}
static void taskYIELD(void) {}
static void call_video_return_slot(unsigned i) {(void)i;}
static void call_video_return_decoded_slot(unsigned i) {(void)i;}
static unsigned call_video_input_queue_depth(void) {return 24;}
#define pdMS_TO_TICKS(ms) (ms)
''' + config + '\n' + helper + r'''
static void decode_boundary(void) {
    unsigned index=0;
    static uint32_t idle_window_countdown=30;
    static int64_t next_idle_window_us;
    (void)idle_window_countdown;(void)next_idle_window_us;
''' + decoder + r'''
}
static void convert_boundary(void) {
    unsigned index=0;
    static int64_t next_idle_window_us;
    (void)next_idle_window_us;
''' + converter + r'''
}
int main(void) {
    /* Both workers stay backlogged for 12 seconds at the frozen 5 fps. */
    for(unsigned i=0;i<60;i++) {
        unsigned before=blocks;
        decode_boundary();assert(blocks>before);
        before=blocks;
        convert_boundary();assert(blocks>before);
        now_us+=200000;
    }
    /* Wall time, rather than 30 frames (~6 s), bounds periodic windows. */
    assert(long_blocks>=30);
}
''')

if __name__ == '__main__':
    unittest.main()
