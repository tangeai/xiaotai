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
 print_audio_timing(media);assert(strstr(output,"decode/lock/write-max=1200/0/0") && !strstr(output,"unavailable"));
}
''')

if __name__ == '__main__': unittest.main()
