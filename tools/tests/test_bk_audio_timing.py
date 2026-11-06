"""Run production Opus ingress/dequeue and timing with deterministic clock gaps."""
import unittest
from test_s3_product_regressions import ROOT, function, run_c
AUDIO=ROOT/'platforms/beken/runtime/src/xiaotai_audio.c'

def diagnostic_block(source):
    return source.split('/* AI audio timing begin */',1)[1].split('/* AI audio timing end */',1)[0]

class AudioTiming(unittest.TestCase):
    def test_packet_timestamps_follow_the_actual_queue(self):
        s=AUDIO.read_text()
        self.assertIn('/* AI audio timing begin */',s)
        run_c(r'''
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdarg.h>
#define OPUS_PACKET_SLOTS 64
#define OPUS_MAX_PACKET_BYTES 512
#define XIAOTAI_AUDIO_CODEC_OPUS_16K 1
#define BK_OK 0
#define BK_ERR_PARAM -1
#define BK_ERR_NOT_INIT -2
#define BK_ERR_BUSY -3
static char logs[16000];
static void log_msg(const char *fmt,...){va_list a;va_start(a,fmt);size_t n=strlen(logs);vsnprintf(logs+n,sizeof(logs)-n,fmt,a);va_end(a);}
#define BK_LOGI(tag,...) log_msg(__VA_ARGS__)
static int log_level=3;
#define BK_LOGD(tag,...) do {if(log_level>=4) log_msg(__VA_ARGS__);} while(0)
#define BK_LOGW(tag,...) log_msg(__VA_ARGS__)
static unsigned now=1000,lock_delay;
static uint32_t rtos_get_time(void){return now;}
static bool s_running=true,s_play_run=true,s_ring_mutex_ready=true;
static int s_ring_mutex,s_codec=1;
static int rtos_trylock_mutex(int *m){return lock_delay?-1:0;}
static int rtos_lock_mutex_timeout(int *m,unsigned n){now+=lock_delay;lock_delay=0;return 0;}
static void rtos_unlock_mutex(int *m){}
static uint8_t storage[64*512],*s_opus_ring=storage;
static uint16_t s_opus_lengths[64];
static size_t s_opus_head,s_opus_count,s_opus_queue_peak;
static unsigned s_dropped_bytes;
static atomic_bool s_playback_in_progress;
static atomic_uint s_admission_busy,s_opus_received_frames,s_last_opus_rx_ms,s_last_downlink_ms;
''' + diagnostic_block(s) + function(s,'xiaotai_audio_play_opus') + function(s,'opus_ring_take') + r'''
int main(void){
 ai_playback_timing_t quiet={0}; ai_uplink_timing_t quiet_up={0};
 ai_audio_written(&quiet,100,102,103,123,true);
 assert(strstr(logs,"AI_AUDIO event=first_write") && strstr(logs,"rx_to_write_ms=123"));
 size_t quiet_len=strlen(logs);
 ai_audio_written(&quiet,1100,1102,1103,1123,true);
 ai_audio_idle(&quiet,1400);
 ai_audio_uplink_timing(&quiet_up,100,120,123,125,true,true,true,0);
 assert(strlen(logs)==quiet_len); /* INFO must suppress periodic details. */
 assert(quiet.frames==2 && quiet_up.sent==1); /* Measurements continue. */
 log_level=4;
 uint8_t p=42,out;lock_delay=3;
 assert(xiaotai_audio_play_opus(&p,1)==1);
 now=1040;assert(opus_ring_take(&out,1)==1 && out==42);
 assert(s_ai_packet.seq==1 && s_ai_packet.rx_ms==1000 && s_ai_packet.enqueue_ms==1003);
 ai_playback_timing_t stats={0};
 ai_audio_written(&stats,1040,1042,1043,1063,true);
 assert(strstr(logs,"rx_ms=1000") && strstr(logs,"admit_ms=3 queue_ms=37 decode_ms=2 prepare_ms=1 write_ms=20 rx_to_write_ms=63"));
 now=2000;p=43;assert(xiaotai_audio_play_opus(&p,1)==1);
 now=2001;assert(opus_ring_take(&out,1)==1 && out==43);
 ai_audio_written(&stats,2001,2002,2002,2022,false);
 assert(strstr(logs,"gap_ms=1000") && strstr(logs,"ok=0"));
 /* Oldest eviction must evict its diagnostic stamp too. */
 for(unsigned i=0;i<80;i++){now=3000+i;p=i;assert(xiaotai_audio_play_opus(&p,1)==1);}
 now=4000;assert(opus_ring_take(&out,1)==1 && out==16);
 assert(s_ai_packet.rx_ms==3016 && s_ai_packet.seq==19);
 ai_audio_written(&stats,4000,4001,4001,4021,true);
 assert(strstr(logs,"queue_max_ms=984"));
 ai_audio_idle(&stats,4221);assert(strstr(logs,"event=idle"));
 size_t len=strlen(logs);ai_audio_idle(&stats,5000);assert(strlen(logs)==len);
 /* Uplink measurements include NS activity only when its result is valid. */
 ai_uplink_timing_t up={0};
 ai_audio_uplink_timing(&up,100,120,123,125,true,true,true,0);
 assert(strstr(logs,"read_max_ms=20 dsp_max_ms=3 send_max_ms=2"));
 ai_audio_uplink_timing(&up,1100,1120,1124,1126,true,true,false,-1);
 assert(strstr(logs,"last_ns_speech_ms=120") && strstr(logs,"rejected=1"));
 puts("AI audio timing PASS: admission=3 ms queue=37 ms decode=2 ms write=20 ms; overflow stamp ownership and idle covered");
}
''')

    def test_signal_queue_delay(self):
        s=(ROOT/'platforms/beken/runtime/src/xiaotai_tirtc.c').read_text()
        run_c(r'''
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#define BK_LOGI(tag,...) snprintf(logs,sizeof(logs),__VA_ARGS__)
static int log_level=3;
#define BK_LOGD(tag,...) do {if(log_level>=4) snprintf(logs,sizeof(logs),__VA_ARGS__);} while(0)
static char logs[512];
static uint32_t now;
static uint32_t rtos_get_time(void){return now;}
typedef struct {uint32_t generation,received_ms,length;} media_event_t;
''' + function(s,'ai_signal_timing') + r'''
int main(void){
 media_event_t quiet={6,1,50};now=30;ai_signal_timing(&quiet);assert(logs[0]==0);log_level=4;
 media_event_t e={7,100,50};now=125;ai_signal_timing(&e);
 assert(strstr(logs,"queue_ms=25 queue_max_ms=25"));
 e.received_ms=200;now=300;ai_signal_timing(&e);
 e.received_ms=1200;now=1201;ai_signal_timing(&e);
 assert(strstr(logs,"queue_max_ms=100"));
 e.generation=8;e.received_ms=1300;now=1302;ai_signal_timing(&e);
 assert(strstr(logs,"generation=8") && strstr(logs,"queue_max_ms=2"));
}
''')

    def test_public_logger_filters_before_formatting(self):
        source=(ROOT/'platforms/beken/runtime/src/xiaotai_log.c').read_text()
        run_c(r'''
#include <assert.h>
#include <stdarg.h>
#include <stdio.h>
#define XIAOTAI_LOG_NONE 0
#define XIAOTAI_APP_LOG_MAX 4
#define BK_ERR_PARAM -1
#define BK_OK 0
static volatile int s_app_log_level=3;
static unsigned formatted,written;
static int counted_format(char *p,size_t n,const char *f,va_list a){formatted++;return vsnprintf(p,n,f,a);}
#define vsnprintf counted_format
static void bk_printf_nonblock(int level,char *tag,const char *fmt,...){written++;}
''' + function(source,'xiaotai_log_set_level') + function(source,'xiaotai_log_write') + r'''
int main(void){
 xiaotai_log_write(4,"audio","sample=%d",123);assert(formatted==0 && written==0);
 xiaotai_log_write(3,"audio","summary");assert(formatted==1 && written==1);
 assert(xiaotai_log_set_level(4)==0);
 xiaotai_log_write(4,"audio","sample");assert(formatted==2 && written==2);
 assert(xiaotai_log_set_level(3)==0);
 xiaotai_log_write(4,"audio","sample");assert(formatted==2 && written==2);
 xiaotai_log_write(2,"audio","warning");assert(written==3);
 xiaotai_log_write(1,"audio","error");assert(written==4);
}
''')

    def test_playback_and_capture_use_real_boundaries(self):
        s=AUDIO.read_text()
        for name,needle in [('playback_task','ai_audio_written('),('playback_task','ai_audio_idle('),('capture_task','ai_audio_uplink_timing(')]:
            self.assertIn(needle,function(s,name))

if __name__=='__main__': unittest.main()
