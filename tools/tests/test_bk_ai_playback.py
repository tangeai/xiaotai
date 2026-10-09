"""Exercise BK's public Opus admission and the production dequeue boundary."""
import re
import unittest
from test_s3_product_regressions import ROOT, function, run_c

AUDIO = ROOT / 'firmware/beken/bk7258/lckfb-bk7258-xiaotai/ap/src/xiaotai_audio.c'


class BKAIPlayback(unittest.TestCase):
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
    s_downlink_frames,s_playback_pcm_ms,s_playback_tail_ms;
static char log_text[512];
static bool s_ring_mutex_ready=true,locked;
static int s_ring_mutex,s_codec=1;
static uint8_t storage[OPUS_PACKET_SLOTS*OPUS_MAX_PACKET_BYTES],*s_opus_ring=storage;
static uint16_t s_opus_lengths[OPUS_PACKET_SLOTS];
static size_t s_opus_head,s_opus_count,s_opus_queue_peak;
static uint32_t s_dropped_bytes;
static uint32_t rtos_get_time(void){return 1000;}
static int rtos_trylock_mutex(int *m){if(locked)return BK_ERR_BUSY;locked=true;return BK_OK;}
static void rtos_unlock_mutex(int *m){assert(locked);locked=false;}
''' + function(source, 'xiaotai_audio_play_opus') + function(source, 'opus_ring_take')
              + function(source, 'xiaotai_audio_log_playback_status') + r'''
int main(void){
    (void)s_opus_received_frames;(void)s_opus_queue_peak;
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
