"""Exercise BK's public Opus admission and the production dequeue boundary."""
import re
import unittest
from test_s3_product_regressions import ROOT, function, run_c

AUDIO = ROOT / 'platforms/beken/runtime/src/xiaotai_audio.c'


class BKAIPlayback(unittest.TestCase):
    def test_drain_snapshot_preserves_final_write_tail(self):
        source = AUDIO.read_text()
        policy = (AUDIO.parent / 'xiaotai_audio_policy.c').read_text()
        run_c(r'''
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include <stdatomic.h>
#define BK_OK 0
#define XIAOTAI_AUDIO_CODEC_OPUS_16K 1
static atomic_bool s_running=true,s_playback_in_progress;
static atomic_uint s_playback_tail_ms;
static bool s_ring_mutex_ready=true,locked,finish_during_snapshot;
static int s_ring_mutex,s_codec=1;
static size_t s_opus_count,s_ring_count;
static uint32_t now_ms=1000;
static uint32_t rtos_get_time(void){return now_ms;}
static int rtos_trylock_mutex(int *m){if(locked)return -1;locked=true;return BK_OK;}
static void rtos_unlock_mutex(int *m){assert(locked);locked=false;}
/* GCC evaluates the old call's tail argument before its active argument.
 * Model the sink completing after the old tail was read, before active=false
 * is read. Explicit snapshots must protect either compiler evaluation order. */
#undef atomic_load_explicit
#define atomic_load_explicit(ptr, order) ({ \
    __auto_type p = (ptr); \
    __auto_type value = __atomic_load_n(p, order); \
    if ((void *)p == (void *)&s_playback_tail_ms && finish_during_snapshot) { \
        finish_during_snapshot=false; \
        atomic_store(&s_playback_tail_ms, now_ms+20); \
        atomic_store(&s_playback_in_progress, false); \
    } \
    value; \
})
''' + function(policy, 'xiaotai_audio_playback_drained')
              + function(source, 'xiaotai_audio_playback_is_drained') + r'''
int main(void){
    s_opus_count=7;assert(!xiaotai_audio_playback_is_drained(0));
    s_opus_count=0;s_playback_in_progress=true;
    assert(!xiaotai_audio_playback_is_drained(0));
    finish_during_snapshot=true;
    assert(!xiaotai_audio_playback_is_drained(200) &&
           "final write racing drain check must retain hardware tail");
    assert(!finish_during_snapshot && !s_playback_in_progress);
    assert(!xiaotai_audio_playback_is_drained(0));
    now_ms+=20;assert(xiaotai_audio_playback_is_drained(0));
    assert(!xiaotai_audio_playback_is_drained(100));
    now_ms+=100;assert(xiaotai_audio_playback_is_drained(100));
    locked=true;assert(!xiaotai_audio_playback_is_drained(0));locked=false;
    s_running=false;assert(xiaotai_audio_playback_is_drained(0));
}
''')

    def replay(self, scenario):
        source = AUDIO.read_text()
        constants = '\n'.join(re.findall(r'^#define OPUS_(?:PACKET_SLOTS|MAX_PACKET_BYTES) .*$', source, re.M))
        run_c(r'''
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include <stdatomic.h>
#include <stdio.h>
#define BK_OK 0
#define BK_ERR_PARAM -1
#define BK_ERR_NOT_INIT -2
#define BK_ERR_BUSY -3
#define XIAOTAI_AUDIO_CODEC_OPUS_16K 1
#define BK_LOGW(...) ((void)0)
#define BK_LOGI(tag,...) snprintf(log_text,sizeof(log_text),__VA_ARGS__)
''' + constants + r'''
static atomic_bool s_running=true,s_play_run=true,s_playback_in_progress;
static atomic_uint s_last_downlink_ms,s_opus_received_frames,s_last_opus_rx_ms,
    s_downlink_frames,s_playback_pcm_ms,s_playback_tail_ms,s_admission_busy;
static char log_text[512];
static bool s_ring_mutex_ready=true,locked,release_on_wait,stop_on_wait;
static int s_ring_mutex,s_codec=1;
static uint8_t storage[OPUS_PACKET_SLOTS*OPUS_MAX_PACKET_BYTES],*s_opus_ring=storage;
static uint16_t s_opus_lengths[OPUS_PACKET_SLOTS];
static size_t s_opus_head,s_opus_count,s_opus_queue_peak;
static uint32_t s_dropped_bytes;
static uint32_t rtos_get_time(void){return 1000;}
static int rtos_trylock_mutex(int *m){if(locked)return BK_ERR_BUSY;locked=true;return BK_OK;}
static int rtos_lock_mutex_timeout(int *m,unsigned ticks){
    assert(ticks==1);if(stop_on_wait)s_running=false;if(release_on_wait){locked=false;release_on_wait=false;}
    return rtos_trylock_mutex(m);
}
static void rtos_unlock_mutex(int *m){assert(locked);locked=false;}
''' + function(source, 'xiaotai_audio_play_opus') + function(source, 'opus_ring_take')
              + function(source, 'xiaotai_audio_log_playback_status') + r'''
int main(void){
    (void)s_opus_received_frames;(void)s_opus_queue_peak;(void)s_admission_busy;
''' + scenario + r'''
}
''')

    def test_tts_burst_preserves_every_packet_in_order(self):
        self.replay(r'''
    for(unsigned i=0;i<56;i++){uint8_t packet=i;assert(xiaotai_audio_play_opus(&packet,1)==1);}
    assert(s_opus_count==56 && s_dropped_bytes==0);
    xiaotai_audio_log_playback_status("end-session");
    assert(strstr(log_text,"rx=56 written=0 overflow=0 pending=56 peak=56 slots=64"));
    for(unsigned i=0;i<56;i++){
        uint8_t packet=255;assert(opus_ring_take(&packet,1)==1 && packet==i);
        s_playback_in_progress=false;
    }
    assert(s_opus_count==0 && opus_ring_take(storage,1)==0);
    s_downlink_frames=56;s_playback_pcm_ms=1120;s_playback_tail_ms=1020;
    xiaotai_audio_log_playback_status("transport-close");
    assert(strstr(log_text,"rx=56 written=56 overflow=0 pending=0"));
    assert(strstr(log_text,"pcm-ms=1120 tail-ms=20"));
    for(unsigned i=0;i<80;i++){uint8_t packet=i;assert(xiaotai_audio_play_opus(&packet,1)==1);}
    assert(s_opus_count==64 && s_dropped_bytes==16); /* bounded oldest eviction */
    locked=true;assert(xiaotai_audio_play_opus(storage,1)==BK_ERR_BUSY);locked=false;
    s_running=false;assert(xiaotai_audio_play_opus(storage,1)==BK_ERR_NOT_INIT);
    assert(xiaotai_audio_play_opus(NULL,1)==BK_ERR_PARAM);
''')

    def test_transient_contention_does_not_discard_audio(self):
        self.replay(r'''
    uint8_t packet=42,output=0;
    locked=true;release_on_wait=true;
    assert(xiaotai_audio_play_opus(&packet,1)==1 &&
           "a short queue/AEC lock handoff must not discard a received packet");
    assert(opus_ring_take(&output,1)==1 && output==42);
    locked=true;
    assert(xiaotai_audio_play_opus(&packet,1)==BK_ERR_BUSY);
    locked=false;
    assert(s_admission_busy==1);
    xiaotai_audio_log_playback_status("drained");
    assert(strstr(log_text,"busy=1"));
''')

    def test_stop_during_lock_handoff_rejects_without_queueing(self):
        self.replay(r'''
    uint8_t packet=42;
    locked=true;release_on_wait=true;stop_on_wait=true;
    assert(xiaotai_audio_play_opus(&packet,1)==BK_ERR_NOT_INIT);
    assert(!locked && s_opus_count==0 && s_admission_busy==0);
''')

    def test_last_packet_stays_active_while_decoder_owns_it(self):
        self.replay(r'''
    uint8_t packet=42,output=0;
    assert(xiaotai_audio_play_opus(&packet,1)==1);
    assert(opus_ring_take(&output,1)==1 && output==42);
    assert(s_opus_count==0);
    assert(s_playback_in_progress && "decoder owns the last packet before I2S write begins");
''')


if __name__ == '__main__':
    unittest.main()
