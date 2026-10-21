"""Replay capture/playback boundaries through production observation snapshots."""
import re
import unittest
from test_s3_product_regressions import ROOT, MEDIA, function, run_c

COMMON = ROOT / 'platforms/esp-idf/components/starter_media_common/include/starter_media.h'
BOARD = ROOT / 'boards/m5stack/cores3-lite/board_adapter.c'

class AudioObservation(unittest.TestCase):
    def test_same_capture_window_and_session_boundaries(self):
        source = MEDIA.read_text()
        header = COMMON.read_text()
        typedef = re.search(r'typedef struct \{[^}]+\} starter_media_audio_diagnostics_t;', header).group()
        # Compile the actual accumulator and its public, non-consuming getter.
        start = source.index('/* Capture-owned accumulator,')
        end = source.index('static atomic_uint_fast32_t s_aec_errors;', start)
        code = source[start:end]
        self.assertIn('audio_observation_sample', code)
        run_c(r'''
#include <assert.h>
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include <string.h>
#include <stdatomic.h>
#include "xiaotai_board_audio.h"
#define portMUX_TYPE int
#define portMUX_INITIALIZER_UNLOCKED 0
#define portENTER_CRITICAL(p) ((void)(p))
#define portEXIT_CRITICAL(p) ((void)(p))
#define AUDIO_HW_SAMPLE_RATE_HZ 16000U
static unsigned clock_ms,dma_ms,pcm_ms,overflows;
static atomic_uint s_generation=1,s_mode=2,s_audio_sent,s_audio_received,s_audio_dropped;
static atomic_uint s_audio_playback_pending;
static atomic_bool s_active=true,s_audio_playback_active;
static int64_t esp_timer_get_time(void){return (int64_t)clock_ms*1000;}
static void audio_playback_timing(uint32_t *p,uint32_t *d){*p=pcm_ms;*d=dma_ms;}
static bool probe(void*c,uint32_t *out){*out=overflows;return true;}
static xiaotai_board_audio_adapter_t adapter={.capture_overflows=probe};
static const xiaotai_board_audio_adapter_t *s_audio_adapter=&adapter;
typedef struct {uint32_t rms,peak;int32_t dc;} level_t;
typedef struct {level_t level[3];uint32_t measurement_us,mic_clipped,reference_clipped;size_t samples_16k;} starter_aec_output_t;
''' + typedef + '\n' + code + r'''
static starter_aec_output_t frame={.samples_16k=512,.mic_clipped=3,.level={{100,32768,0},{10,100,0},{50,1000,0}}};
static void block(unsigned duration){
 audio_observation_sample_t begin=audio_observation_sample();
 clock_ms+=duration;s_audio_sent+=2;s_audio_received++;overflows++;
 audio_observation_sample_t end=audio_observation_sample();
 if(update_audio_observation(&begin,&end,&frame))update_audio_diagnostics(&frame,44000,end.at_ms);
}
int main(void){
 starter_media_audio_diagnostics_t out,again;
 /* Queue-only prefill must not claim DMA output; later tail survives idle worker. */
 s_audio_playback_pending=12;
 for(int i=0;i<16;i++)block(48);
 s_audio_playback_pending=0;dma_ms=20;
 for(int i=0;i<16;i++)block(48);
 starter_media_audio_diagnostics(&out);
 assert(out.correlated && out.frames==32 && out.wall_ms==1536 && out.audio_ms==1024);
 assert(out.generation==1 && out.tx_delta==64 && out.rx_delta==32);
 assert(out.capture_overflow_supported && out.capture_overflow_delta==32);
 assert(out.worker_frames==0 && out.dma_frames==16 && out.pending_peak==12);
 assert(out.dma_peak_ms==20 && out.mic_clipped==96 && out.reference_clipped==0);
 assert(out.rms_avg[0]==100 && out.peak[0]==32768 && out.aec_avg_us==44000);
 starter_media_audio_diagnostics(&again);assert(!memcmp(&out,&again,sizeof out));
 /* A new session discards partial data and never subtracts reset session counters. */
 for(int i=0;i<7;i++)block(32);
 s_generation=2;s_audio_sent=0;s_audio_received=0;dma_ms=0;
 for(int i=0;i<32;i++)block(32);
 starter_media_audio_diagnostics(&out);
 assert(out.window==2 && out.generation==2 && out.tx_delta==64 && out.wall_ms==1024);
 assert(out.dma_frames==0 && out.mic_clipped==96);
 /* Missing board probe is unknown, not a proven zero loss. */
 adapter.capture_overflows=NULL;
 for(int i=0;i<32;i++)block(32);
 starter_media_audio_diagnostics(&out);assert(!out.capture_overflow_supported);
 /* A reset racing within the capture read must not publish a mixed frame. */
 audio_observation_sample_t begin=audio_observation_sample();s_generation=3;
 audio_observation_sample_t end=audio_observation_sample();
 assert(!update_audio_observation(&begin,&end,&frame));
 /* Millisecond wrap is a valid bounded window, not billions of ms. */
 clock_ms=UINT32_MAX-511U;
 for(int i=0;i<32;i++)block(32);
 starter_media_audio_diagnostics(&out);assert(out.wall_ms==1024);
 /* Reset observed before generation changes: mark counter interval invalid. */
 s_generation=4;s_audio_sent=0;s_audio_received=0;
 for(int i=0;i<16;i++)block(32);
 s_audio_sent=0;s_audio_received=0;
 for(int i=0;i<16;i++)block(32);
 starter_media_audio_diagnostics(&out);assert(!out.counters_valid);
}
''', [ROOT / 'platforms/common/include'])

    def test_console_distinguishes_unknown_and_correlated_windows(self):
        source = (ROOT / 'platforms/esp-idf/components/starter_console/src/starter_console.c').read_text()
        typedef = re.search(r'typedef struct \{[^}]+\} starter_media_audio_diagnostics_t;', COMMON.read_text()).group()
        run_c(r'''
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
#include <stdarg.h>
#include <string.h>
static char text[4096];static size_t used;
static int capture_printf(const char *fmt,...){va_list a;va_start(a,fmt);
 int n=vsnprintf(text+used,sizeof text-used,fmt,a);va_end(a);used+=n;return n;}
#define printf capture_printf
''' + typedef + function(source,'print_audio_observation') + r'''
int main(void){
 starter_media_audio_diagnostics_t d={0};print_audio_observation(&d);
 assert(strstr(text,"unavailable"));
 used=0;text[0]=0;d.correlated=true;d.frames=32;d.wall_ms=1536;d.audio_ms=1024;
 d.worker_frames=0;d.dma_frames=16;d.pending_peak=12;d.mic_clipped=96;
 print_audio_observation(&d);
 assert(strstr(text,"wall/audio-ms=1536/1024"));
 assert(strstr(text,"worker/dma-frames=0/16"));
 assert(strstr(text,"capture-overflow=unknown"));
 assert(strstr(text,"tx/rx/drop=unknown"));
 used=0;text[0]=0;d.capture_overflow_supported=true;d.capture_overflow_delta=9;
 d.counters_valid=true;d.tx_delta=48;d.rx_delta=3;
 print_audio_observation(&d);
 assert(strstr(text,"capture-overflow=9"));assert(strstr(text,"tx/rx/drop=48/3/0"));
}
''')

    def test_board_overflow_callback_is_read_only_and_bounded(self):
        source = BOARD.read_text()
        funcs = ''.join(function(source,n) for n in (
            'audio_capture_overflow', 'audio_capture_probe_init', 'audio_adapter_capture_overflows'))
        run_c(r'''
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#define IRAM_ATTR
#define DRAM_ATTR
#define portMUX_TYPE int
#define portMUX_INITIALIZER_UNLOCKED 0
#define portENTER_CRITICAL_ISR(p) ((void)(p))
#define portEXIT_CRITICAL_ISR(p) ((void)(p))
#define portENTER_CRITICAL(p) ((void)(p))
#define portEXIT_CRITICAL(p) ((void)(p))
#define ESP_OK 0
typedef int esp_err_t;
typedef void *i2s_chan_handle_t;
typedef struct {size_t size;} i2s_event_data_t;
typedef bool(*callback_t)(i2s_chan_handle_t,i2s_event_data_t*,void*);
typedef struct {callback_t on_recv_q_ovf;} i2s_event_callbacks_t;
static i2s_chan_handle_t s_i2s_rx=(void*)1;
static uint32_t s_capture_overflows;
static portMUX_TYPE s_capture_probe_lock;
static bool s_capture_probe_ready;
static callback_t callback;
static int registration_error;
static int i2s_channel_register_event_callback(i2s_chan_handle_t ch,const i2s_event_callbacks_t *cb,void *ctx){
 assert(ch==s_i2s_rx);callback=cb->on_recv_q_ovf;return registration_error;
}
''' + funcs + r'''
int main(void){
 uint32_t count=99;
 assert(!audio_adapter_capture_overflows(NULL,&count));
 registration_error=7;assert(audio_capture_probe_init()==7);
 assert(!audio_adapter_capture_overflows(NULL,&count));
 registration_error=0;assert(audio_capture_probe_init()==0 && callback);
 i2s_event_data_t event={.size=1024};
 for(int i=0;i<3;i++)assert(!callback(s_i2s_rx,&event,NULL));
 assert(audio_adapter_capture_overflows(NULL,&count) && count==3);
 assert(audio_adapter_capture_overflows(NULL,&count) && count==3);
 assert(!audio_adapter_capture_overflows(NULL,NULL));
}
''')

if __name__=='__main__': unittest.main()
