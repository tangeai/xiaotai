"""Unmeasured adapter counters must not masquerade as zero-cost processing."""
import unittest
from test_s3_product_regressions import ROOT, function, run_c

class StatusMetricValidity(unittest.TestCase):
    def test_missing_audio_timing_is_explicit(self):
        source = (ROOT / 'platforms/esp-idf/components/starter_console/src/starter_console.c').read_text()
        if 'static void print_audio_timing(' in source:
            code = function(source, 'print_audio_timing')
        else:
            begin = source.index('    if (media.audio_rx_queue_capacity != 0U) {')
            end = source.index('    printf("AEC:', begin)
            code = 'static void print_audio_timing(starter_media_status_t media){\n' + source[begin:end] + '}\n'
        run_c(r'''
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdarg.h>
#include <string.h>
typedef struct {unsigned audio_rx_queue_capacity,audio_rx_overflow,audio_rx_prefill_waits,audio_rx_burst_max,audio_rx_queue_peak,audio_pcm_last_samples,audio_decode_max_us,audio_lock_max_us,audio_write_max_us,audio_encode_avg_us,audio_decode_avg_us,audio_lock_avg_us,audio_write_avg_us,audio_encode_max_us;} starter_media_status_t;
static char output[4096];
static int capture(const char *format,...){va_list args;va_start(args,format);int n=vsnprintf(output+strlen(output),sizeof(output)-strlen(output),format,args);va_end(args);return n;}
#define printf capture
''' + code + r'''
int main(void){starter_media_status_t media={.audio_rx_queue_capacity=64,.audio_rx_queue_peak=43};
 print_audio_timing(media);
 assert(strstr(output,"queue-peak=43") && strstr(output,"unavailable"));
 assert(!strstr(output,"decode/lock/write-max=0/0/0"));
 output[0]=0;media.audio_pcm_last_samples=320;media.audio_decode_max_us=1200;
 print_audio_timing(media);assert(strstr(output,"decode/lock/write-max=1200/0/0") && strstr(output,"Audio encode timing: unavailable"));
}
''')

    def test_gap_observation_reports_same_frame_without_claiming_a_stutter(self):
        source = (ROOT / 'platforms/esp-idf/components/starter_console/src/starter_console.c').read_text()
        header = (ROOT / 'platforms/esp-idf/components/starter_media_common/include/starter_media.h').read_text()
        status_type = header[header.index('typedef struct {'):header.index('} starter_media_status_t;') + len('} starter_media_status_t;')]
        run_c(r'''
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdarg.h>
#include <string.h>
typedef int starter_tirtc_mode_t;
''' + status_type + r'''
static char output[4096];
static int capture(const char *format,...){va_list args;va_start(args,format);int n=vsnprintf(output+strlen(output),sizeof(output)-strlen(output),format,args);va_end(args);return n;}
#define printf capture
''' + function(source, 'print_audio_gap_observation') + r'''
int main(void){starter_media_status_t media={0};
 print_audio_gap_observation(media);assert(output[0]==0);
 media.audio_trace_supported=true;
 print_audio_gap_observation(media);assert(strstr(output,"no complete observation"));
 output[0]=0;media.audio_trace_frame=2500;media.audio_trace_at_ms=255569;
 media.audio_trace_rx_gap_ms=140;media.audio_trace_queue_ms=80;
 media.audio_trace_write_gap_ms=160;media.audio_trace_decode_us=1200;
 media.audio_trace_lock_us=700;media.audio_trace_write_us=20000;
 media.audio_trace_pending=4;media.audio_rx_prefill_target_ms=240;media.audio_rx_rebuffers=2;
 print_audio_gap_observation(media);
 assert(strstr(output,"Audio gap observation:") && strstr(output,"at-ms=255569 frame=2500"));
 assert(strstr(output,"write-gap-ms=160 rx-gap-ms=140 queue-ms=80 pending=4"));
 assert(strstr(output,"decode/lock/write=1200/700/20000 us"));
 assert(strstr(output,"Audio AI buffer: target-ms=240 rebuffers=2"));
}
''')

if __name__ == '__main__': unittest.main()
