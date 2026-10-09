"""Replay P4 downlink PCM writes and observe the public media drain status."""
import re
import unittest

from test_s3_product_regressions import ROOT, function, run_c
from test_s3_opus_media import COMMON

P4_MEDIA = ROOT / 'firmware/esp-idf/esp32p4/waveshare-touch-lcd-43c/components/starter_media/src/starter_media.c'


class P4AIPlayback(unittest.TestCase):
    def test_ai_burst_retains_frames_and_preserves_live_call_budget(self):
        source = P4_MEDIA.read_text()
        constants = '\n'.join(line for line in source.splitlines()
                              if line.startswith(('#define AUDIO_RX_QUEUE_', '#define AUDIO_RX_LIVE_', '#define AUDIO_RX_PREFILL_')))
        run_c(r'''
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include <stdatomic.h>
#include <setjmp.h>
#define pdTRUE 1
#define pdFALSE 0
#define portMAX_DELAY 999999U
#define pdMS_TO_TICKS(x) (x)
#define ESP_LOGE(...) ((void)0)
#define STARTER_TIRTC_AI 2
#define AUDIO_RX_BYTES 1500U
#define AUDIO_PLAYBACK_IDLE_MS 60U
''' + constants + r'''
typedef int starter_tirtc_mode_t;
typedef struct {size_t length;unsigned sequence;} starter_tirtc_frame_t;
typedef struct {int mode;uint32_t generation;starter_tirtc_frame_t frame;
    uint8_t payload[AUDIO_RX_BYTES];} audio_rx_item_t;
typedef struct {uint8_t slots[256];unsigned head,count;} queue_t;
static queue_t ready,free_slots;
static queue_t *s_audio_rx_ready_queue=&ready,*s_audio_rx_free_queue=&free_slots;
static audio_rx_item_t pool[AUDIO_RX_QUEUE_DEPTH],*s_audio_rx_pool=pool;
static atomic_uint s_audio_dropped,s_audio_received,s_audio_rx_overflow,
    s_audio_playback_pending,s_audio_rx_last_ms,s_audio_rx_prefill_waits,
    s_audio_rx_burst_max,s_audio_rx_burst_window;
static atomic_uint_fast32_t s_audio_rx_queue_peak;
static atomic_bool s_audio_playback_active;
static unsigned now_ms=1000,played;
static int active_mode=2;
static uint32_t generation=7;
static jmp_buf done;
static bool same_session(int m,uint32_t g){return m==active_mode && g==generation;}
static int64_t esp_timer_get_time(void){return (int64_t)now_ms*1000;}
static unsigned uxQueueMessagesWaiting(queue_t *q){return q->count;}
static int xQueueSend(queue_t *q,const uint8_t *v,unsigned timeout){
    if(q->count==AUDIO_RX_QUEUE_DEPTH)return pdFALSE;
    q->slots[(q->head+q->count)%256]=*v;q->count++;return pdTRUE;
}
static int xQueueReceive(queue_t *q,uint8_t *v,unsigned timeout){
    if(!q->count){if(timeout==portMAX_DELAY)longjmp(done,1);return pdFALSE;}
    *v=q->slots[q->head];q->head=(q->head+1)%256;q->count--;return pdTRUE;
}
static void vTaskDelay(unsigned ms){now_ms+=ms;}
static void audio_rx_record_arrival(void){}
static bool play_audio_item(const audio_rx_item_t *item){
    assert(item->frame.sequence==played++);
    now_ms+=20;return true;
}
''' + (function(source, 'update_max_counter') if 'static void update_max_counter(' in source else '')
              + (function(source, 'audio_rx_prefill') if 'static bool audio_rx_prefill(' in source else '')
              + function(source, 'starter_media_submit_audio')
              + function(source, 'audio_sink_task')
              + function(source, 'drain_audio_rx_ready_queue') + r'''
static void submit(unsigned count){
    for(unsigned i=0;i<count;i++){
        starter_tirtc_frame_t frame={.length=31,.sequence=i};uint8_t data[31]={0};
        starter_media_submit_audio(active_mode,generation,&frame,data);
    }
}
int main(void){
    (void)s_audio_rx_prefill_waits;(void)s_audio_rx_queue_peak;
    (void)s_audio_rx_burst_max;(void)s_audio_rx_burst_window;
    for(unsigned i=0;i<AUDIO_RX_QUEUE_DEPTH;i++){uint8_t slot=i;xQueueSend(&free_slots,&slot,0);}
    submit(56); /* TTS delivery ahead of the physical 50fps playback clock. */
    assert(s_audio_received==56 && s_audio_rx_overflow==0 && "AI burst must not omit speech samples");
    assert(s_audio_playback_pending==56);
    assert(s_audio_rx_queue_peak==56);
    if(!setjmp(done))audio_sink_task(NULL);
    assert(played==56 && now_ms==2120 && "preserve every 20ms frame in order");
    assert(s_audio_playback_pending==0 && !s_audio_playback_active);
    assert(free_slots.count==AUDIO_RX_QUEUE_DEPTH);
    submit(80);assert(ready.count==64 && s_audio_rx_overflow==16); /* bounded */
    drain_audio_rx_ready_queue();assert(s_audio_playback_pending==0);
    active_mode=3;generation=8;submit(16);
    assert(ready.count==8 && "human calls keep their original low-latency budget");
    drain_audio_rx_ready_queue();assert(s_audio_playback_pending==0);
    starter_tirtc_frame_t frame={.length=31};uint8_t data[31]={0};
    starter_media_submit_audio(2,7,&frame,data);assert(ready.count==0); /* stale */
    assert(free_slots.count==AUDIO_RX_QUEUE_DEPTH);
}
''')

    def test_dma_audio_remains_active_after_write_returns(self):
        source = P4_MEDIA.read_text()
        status = function(source, 'starter_media_status')
        header = (ROOT / 'platforms/esp-idf/components/starter_media_common/include/starter_media.h').read_text()
        status_type = header[header.index('typedef struct {'):header.index('} starter_media_status_t;') + len('} starter_media_status_t;')]
        known = set(re.findall(r'\bs_\w+\b', COMMON))
        extra = sorted(set(re.findall(r'&\s*(s_\w+)', status)) - known)
        helpers = ''.join(function(source, name) for name in ('audio_track_dma_write',)
                          if re.search(r'\b' + name + r'\([^;]*?\)\s*\{', source))
        run_c(COMMON + status_type + '\n' + ''.join(f'static atomic_uint_fast32_t {name};\n' for name in extra if name != 's_audio_dma_until_us') + r'''
#define AUDIO_RX_QUEUE_DEPTH 64U
#define AUDIO_RX_LIVE_QUEUE_DEPTH 8U
#define AUDIO_HW_SAMPLE_RATE_HZ 16000U
#define AUDIO_I2S_DMA_DESC_NUM 6U
#define AUDIO_I2S_DMA_FRAME_NUM 240U
static atomic_int_fast64_t s_audio_dma_until_us;
static atomic_int_fast64_t s_audio_playback_started_us;
static int64_t clock_us;
static int64_t esp_timer_get_time(void){return clock_us;}
static unsigned p4_video_sent(void){return 0;}
static bool uplink_allowed(int m){return true;}
static void *s_audio_rx_ready_queue;
static unsigned uxQueueMessagesWaiting(void *q){return 0;}
typedef struct {int mode;uint32_t generation;struct {size_t length;} frame;
    uint8_t payload[1500];} audio_rx_item_t;
static int16_t s_decode_pcm[1500],s_play_stereo[6000];
static uint32_t s_playback_resampler_generation;
static int16_t s_playback_previous;
static int s_audio_output_mutex;
static bool s_amp_enabled=true;
static size_t decoded_samples=320;
static unsigned write_ms;
static bool write_fails;
static int decode(esp_audio_dec_out_frame_t *o){
    for(size_t i=0;i<decoded_samples;i++)((int16_t *)o->buffer)[i]=(int16_t)i;
    o->decoded_size=decoded_samples*2;return 0;
}
static int esp_g711a_dec_decode(void *h,esp_audio_dec_in_raw_t *i,esp_audio_dec_out_frame_t *o,esp_audio_dec_info_t *n){return decode(o);}
static int esp_opus_dec_decode(void *h,esp_audio_dec_in_raw_t *i,esp_audio_dec_out_frame_t *o,esp_audio_dec_info_t *n){return decode(o);}
static int xSemaphoreTake(int h,int t){return pdTRUE;}
static void xSemaphoreGive(int h){}
static int write_pcm(void *context,const int16_t *pcm,size_t frames,size_t *written){
    assert(frames==decoded_samples*(mode==STARTER_TIRTC_AI?1:2));
    if(mode==STARTER_TIRTC_AI)assert(pcm[2]==1 && pcm[3]==1);
    clock_us+=write_ms*1000;
    *written=write_fails ? 0 : frames*4;
    return write_fails ? -1 : 0;
}
static struct {void *context;int (*write_pcm)(void *,const int16_t *,size_t,size_t *);} adapter={NULL,write_pcm},*s_audio_adapter=&adapter;
''' + helpers + function(source, 'play_audio_item') + status + r'''
int main(void){
    audio_rx_item_t item={.mode=STARTER_TIRTC_AI,.generation=7,.frame.length=37};
    (void)s_audio_dma_until_us;
    clock_us=2000000;
    assert(play_audio_item(&item)); /* I2S accepts 20ms without waiting for DAC. */
    starter_media_status_t media=starter_media_status();
    assert(media.audio_played==1 && media.audio_playback_pending==0);
    assert(media.audio_playback_active && "empty software queue does not mean DMA has drained");
    assert(media.audio_playback_pcm_ms==20);
    assert(media.audio_playback_dma_ms>=20 && media.audio_playback_dma_ms<=110);
    clock_us+=19000;assert(starter_media_status().audio_playback_active);
    clock_us+=200000;assert(!starter_media_status().audio_playback_active);
    /* A burst of variable-duration packets must retain every sample's time. */
    decoded_samples=640;
    int64_t start=clock_us;
    for(unsigned i=0;i<10;i++)assert(play_audio_item(&item));
    clock_us=start+399000;assert(starter_media_status().audio_playback_active);
    clock_us=start+600000;assert(!starter_media_status().audio_playback_active);
    /* Blocking writes already spend sample time; it must not be added twice. */
    decoded_samples=320;write_ms=20;
    assert(play_audio_item(&item));
    clock_us+=200000;assert(!starter_media_status().audio_playback_active);
    /* Failures and muted frames must not invent a playback debt. */
    write_fails=true;assert(!play_audio_item(&item));
    assert(!starter_media_status().audio_playback_active);
    write_fails=false;s_speaker_muted=true;assert(!play_audio_item(&item));
    assert(!starter_media_status().audio_playback_active);
    s_speaker_muted=false;item.generation=6;
    assert(!play_audio_item(&item)); /* stale owner must not create DMA debt */
    assert(!starter_media_status().audio_playback_active);
}
''')

    def test_end_logs_distinguish_received_written_and_dma_tail(self):
        source = (ROOT / 'platforms/esp-idf/components/starter_runtime/src/starter_runtime.c').read_text()
        header = (ROOT / 'platforms/esp-idf/components/starter_media_common/include/starter_media.h').read_text()
        status_type = header[header.index('typedef struct {'):header.index('} starter_media_status_t;') + len('} starter_media_status_t;')]
        run_c(r'''
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
typedef int starter_tirtc_mode_t;
''' + status_type + r'''
static starter_media_status_t media;
static uint32_t session_generation(void){return 7;}
static int64_t now_ms(void){return 210000;}
static starter_media_status_t starter_media_status(void){return media;}
static char log_text[512];
#define ESP_LOGI(tag,...) snprintf(log_text,sizeof(log_text),__VA_ARGS__)
''' + function(source, 'log_ai_playback_status') + r'''
int main(void){
    media=(starter_media_status_t){.audio_received=2614,.audio_decoded=2600,
        .audio_played=2599,.audio_rx_overflow=2,.audio_playback_pending=15,
        .audio_playback_active=true,.audio_playback_pcm_ms=51980,
        .audio_playback_dma_ms=90,.audio_rx_last_ms=209950};
    log_ai_playback_status("transport-close");
    assert(strstr(log_text,"stage=transport-close generation=7"));
    assert(strstr(log_text,"rx=2614 decoded=2600 written=2599 overflow=2"));
    assert(strstr(log_text,"pending=15 active=1 pcm-ms=51980 dma-ms=90 last-rx-age-ms=50"));
    media.audio_playback_pending=0;media.audio_playback_active=false;
    media.audio_playback_dma_ms=0;
    log_ai_playback_status("drained");
    assert(strstr(log_text,"stage=drained"));
    assert(strstr(log_text,"pending=0 active=0"));
}
''')


if __name__ == '__main__':
    unittest.main()
