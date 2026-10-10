"""Replay P4 downlink PCM writes and observe the public media drain status."""
import re
import unittest

from test_s3_product_regressions import ROOT, function, run_c
from test_s3_opus_media import COMMON

P4_MEDIA = ROOT / 'firmware/esp-idf/esp32p4/waveshare-touch-lcd-43c/components/starter_media/src/starter_media.c'


class P4AIPlayback(unittest.TestCase):
    def test_start_publishes_active_only_after_output_ready(self):
        body = function(P4_MEDIA.read_text(), 'starter_media_start')
        atomics = sorted(set(re.findall(r'atomic_\w+\(&([a-zA-Z_][a-zA-Z_0-9]*)', body)))
        run_c(r'''
#include <assert.h>
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdatomic.h>
#define ESP_OK 0
#define ESP_ERR_INVALID_STATE -1
#define ESP_ERR_TIMEOUT -2
#define ESP_LOGI(...) ((void)0)
#define ESP_LOGE(...) ((void)0)
#define pdTRUE 1
#define pdMS_TO_TICKS(x) (x)
#define portMAX_DELAY 0
#define STARTER_TIRTC_H5 1
#define STARTER_TIRTC_AI 2
#define STARTER_TIRTC_VOIP 3
#define STARTER_TIRTC_CALL 4
#define STARTER_TIRTC_ROOM 5
typedef int esp_err_t;
typedef int starter_tirtc_mode_t;
''' + 'static atomic_uint '+','.join(atomics)+';\n' + r'''
static int s_preroll_mutex=1,s_audio_output_mutex=2,s_preroll;
static unsigned s_expected_wake_token,canceled,video_started;
static bool amp,timeout,locked;
static int64_t esp_timer_get_time(void){return 1;}
static bool starter_preroll_bind(int*a,unsigned b,unsigned c,int64_t d){return true;}
static void starter_preroll_clear(int*a){}
static void starter_media_cancel_pcm8k_playback(void){canceled++;}
static void drain_audio_rx_ready_queue(void){}
static int xSemaphoreTake(int mutex,int delay){
 if(mutex==2){assert(!s_active && canceled);if(timeout)return 0;locked=true;}return 1;
}
static void xSemaphoreGive(int mutex){if(mutex==2){assert(locked);locked=false;}}
static int audio_output_set_locked(bool enabled){assert(locked&&!s_active);amp=enabled;return 0;}
static void p4_video_set_session(int mode,unsigned gen,bool video){assert(s_active&&amp&&!locked);video_started++;}
''' + body + r'''
int main(void){s_ready=true;
 assert(starter_media_start(3,7)==0&&s_active&&amp&&video_started==1);
 s_active=false;amp=false;timeout=true;
 assert(starter_media_start(3,8)==-2&&!s_active&&s_generation==0&&video_started==1);
}
''')

    def test_ai_burst_retains_frames_and_preserves_live_call_budget(self):
        source = P4_MEDIA.read_text()
        restart_accounting = '\n'.join(re.findall(r'atomic_store_explicit\(&s_audio_playback_(?:pending|active),[^;]+;', function(source, 'starter_media_start')))
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
#define AUDIO_PACKET_MS 20U
''' + constants + r'''
typedef int starter_tirtc_mode_t;
typedef struct {size_t length;unsigned sequence;} starter_tirtc_frame_t;
typedef struct {int mode;uint32_t generation;starter_tirtc_frame_t frame;
    uint32_t received_at_ms,arrival_gap_ms;uint8_t payload[AUDIO_RX_BYTES];} audio_rx_item_t;
typedef struct {uint8_t slots[256];unsigned head,count;} queue_t;
static queue_t ready,free_slots;
static queue_t *s_audio_rx_ready_queue=&ready,*s_audio_rx_free_queue=&free_slots;
static audio_rx_item_t pool[AUDIO_RX_QUEUE_DEPTH],*s_audio_rx_pool=pool;
static atomic_uint s_audio_dropped,s_audio_received,s_audio_rx_overflow,
    s_audio_playback_pending,s_audio_rx_last_ms,s_audio_rx_prefill_waits,
    s_audio_rx_burst_max,s_audio_rx_burst_window,
    s_audio_rx_rebuffers,s_audio_rx_prefill_target_ms;
static atomic_int_fast64_t s_audio_dma_until_us;
static atomic_uint_fast32_t s_audio_rx_queue_peak;
static atomic_bool s_audio_playback_active,s_speaker_muted;
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
static bool restart_in_prefill;
static void restart_accounting(void);
static void drain_audio_rx_ready_queue(void);
static void vTaskDelay(unsigned ms){now_ms+=ms;
    if(restart_in_prefill){restart_in_prefill=false;drain_audio_rx_ready_queue();generation++;restart_accounting();}
}
static void audio_rx_record_arrival(void){}
static bool play_audio_item(const audio_rx_item_t *item){
    assert(item->frame.sequence==played++);
    now_ms+=20;s_audio_dma_until_us=(int64_t)(now_ms+100)*1000;return true;
}
''' + (function(source, 'update_max_counter') if 'static void update_max_counter(' in source else '')
              + (function(source, 'audio_rx_prefill') if 'static bool audio_rx_prefill(' in source else '')
              + function(source, 'starter_media_submit_audio')
              + function(source, 'audio_sink_task')
              + function(source, 'drain_audio_rx_ready_queue') + '\nstatic void restart_accounting(void){' + restart_accounting + '}\n' + r'''
static void submit(unsigned count){
    for(unsigned i=0;i<count;i++){
        starter_tirtc_frame_t frame={.length=31,.sequence=i};uint8_t data[31]={0};
        starter_media_submit_audio(active_mode,generation,&frame,data);
    }
}
int main(void){
    (void)s_speaker_muted;(void)s_audio_rx_prefill_waits;(void)s_audio_rx_queue_peak;
    (void)s_audio_rx_burst_max;(void)s_audio_rx_burst_window;
    for(unsigned i=0;i<AUDIO_RX_QUEUE_DEPTH;i++){uint8_t slot=i;xQueueSend(&free_slots,&slot,0);}
    submit(56); /* TTS delivery ahead of the physical 50fps playback clock. */
    assert(s_audio_received==56 && s_audio_rx_overflow==0 && "AI burst must not omit speech samples");
    assert(s_audio_playback_pending==56);
    assert(pool[0].received_at_ms==1000 && pool[0].arrival_gap_ms==0);
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
    /* Restart while the sink owns one slot in prefill; execute production reset
     * statements while retaining the real queue producer, sink and drainer. */
    active_mode=2;generation=9;s_audio_dma_until_us=0;submit(1);
    restart_in_prefill=true;
    if(!setjmp(done))audio_sink_task(NULL);
    assert(s_audio_playback_pending==0 && "retiring an old held slot must not underflow new-session pending");
    assert(free_slots.count==AUDIO_RX_QUEUE_DEPTH && played==56);
}
''')

    def test_ai_prefill_is_bounded_adaptive_and_cancellable(self):
        source = P4_MEDIA.read_text()
        constants = '\n'.join(line for line in source.splitlines()
                              if line.startswith('#define AUDIO_RX_PREFILL_'))
        run_c(r'''
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdatomic.h>
#define STARTER_TIRTC_AI 2
#define AUDIO_PACKET_MS 20U
#define pdMS_TO_TICKS(x) (x)
''' + constants + r'''
typedef struct {int mode;uint32_t generation;} audio_rx_item_t;
static atomic_int_fast64_t s_audio_dma_until_us;
static atomic_uint_fast32_t s_audio_rx_prefill_waits,s_audio_rx_rebuffers,s_audio_rx_prefill_target_ms;
static int s_audio_rx_ready_queue;
static int mode=2;static uint32_t generation=7;
static int64_t now_us=1000000,wait_start;
static unsigned queued,arrivals,cancel_ms,mute_ms;
static atomic_bool s_speaker_muted;
static bool same_session(int m,uint32_t g){return m==mode && g==generation;}
static int64_t esp_timer_get_time(void){return now_us;}
static unsigned uxQueueMessagesWaiting(int q){return queued;}
static void vTaskDelay(unsigned ms){now_us+=ms*1000;
    if(arrivals && (now_us-wait_start)/20000 > queued && queued<arrivals)queued++;
    if(cancel_ms && now_us-wait_start>=cancel_ms*1000)generation++;
    if(mute_ms && now_us-wait_start>=mute_ms*1000)s_speaker_muted=true;
}
''' + function(source, 'audio_rx_prefill') + r'''
int main(void){audio_rx_item_t item={.mode=2,.generation=7};
 wait_start=now_us;arrivals=9;
 assert(audio_rx_prefill(&item));
 assert(now_us-wait_start==180000 && queued==9);
 assert(s_audio_rx_prefill_target_ms==200 && s_audio_rx_prefill_waits==1);
 /* A temporarily empty software queue with DMA audio left is not an underrun. */
 queued=0;arrivals=0;s_audio_dma_until_us=now_us+100000;wait_start=now_us;
 assert(audio_rx_prefill(&item));assert(now_us==wait_start && s_audio_rx_rebuffers==0);
 /* A drained output raises the budget to 240 ms, bounded to 12 packets. */
 now_us+=120000;wait_start=now_us;arrivals=11;
 assert(audio_rx_prefill(&item));assert(now_us-wait_start==220000);
 assert(s_audio_rx_prefill_target_ms==240 && s_audio_rx_rebuffers==1);
 s_audio_dma_until_us=now_us+20000000;now_us+=11000000;
 assert(audio_rx_prefill(&item));assert(s_audio_rx_prefill_target_ms==200);
 /* A one-packet reply must play even if no more packets ever arrive. */
 generation=8;item.generation=8;queued=0;arrivals=0;wait_start=now_us;
 assert(audio_rx_prefill(&item));assert(now_us-wait_start==240000);
 /* Ending/replacing a session during prefill releases the held slot promptly. */
 generation=9;item.generation=9;wait_start=now_us;cancel_ms=10;
 assert(!audio_rx_prefill(&item));assert(now_us-wait_start==10000);
 cancel_ms=0;mode=2;generation=11;item.generation=11;queued=0;arrivals=0;
 s_speaker_muted=true;wait_start=now_us;
 assert(!audio_rx_prefill(&item));assert(now_us==wait_start);
 s_speaker_muted=false;generation=12;item.generation=12;mute_ms=10;wait_start=now_us;
 assert(!audio_rx_prefill(&item));assert(now_us-wait_start==10000);
 mute_ms=0;s_speaker_muted=false;mode=3;item.mode=3;item.generation=generation;wait_start=now_us;
 assert(audio_rx_prefill(&item));assert(now_us==wait_start);
}
''')

    def test_dma_audio_remains_active_after_write_returns(self):
        source = P4_MEDIA.read_text()
        status = function(source, 'starter_media_status')
        header = (ROOT / 'platforms/esp-idf/components/starter_media_common/include/starter_media.h').read_text()
        status_type = header[header.index('typedef struct {'):header.index('} starter_media_status_t;') + len('} starter_media_status_t;')]
        known = set(re.findall(r'\bs_\w+\b', COMMON))
        extra = sorted(set(re.findall(r'&\s*(s_\w+)', status)) - known)
        helpers = ''.join(function(source, name) for name in ('audio_track_dma_write', 'update_max_counter', 'update_timing_average', 'audio_record_playback_trace')
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
static unsigned info_logs;
#undef ESP_LOGI
#define ESP_LOGI(...) ((void)++info_logs)
#define ESP_LOGD(...) ((void)0)
static int64_t esp_timer_get_time(void){return clock_us;}
static unsigned p4_video_sent(void){return 0;}
static bool uplink_allowed(int m){return true;}
static void *s_audio_rx_ready_queue;
static unsigned uxQueueMessagesWaiting(void *q){return 0;}
typedef struct {int mode;uint32_t generation;struct {size_t length;} frame;
    uint32_t received_at_ms,arrival_gap_ms;uint8_t payload[1500];} audio_rx_item_t;
static int16_t s_decode_pcm[1500],s_play_stereo[6000];
static uint32_t s_playback_resampler_generation;
static int16_t s_playback_previous;
static int s_audio_output_mutex;
static bool s_amp_enabled=true;
static size_t decoded_samples=320;
static unsigned write_ms,decode_us=1200,lock_us=700;
static bool write_fails,lock_fails;
static int decode(esp_audio_dec_out_frame_t *o){
    clock_us+=decode_us;
    for(size_t i=0;i<decoded_samples;i++)((int16_t *)o->buffer)[i]=(int16_t)i;
    o->decoded_size=decoded_samples*2;return 0;
}
static int esp_g711a_dec_decode(void *h,esp_audio_dec_in_raw_t *i,esp_audio_dec_out_frame_t *o,esp_audio_dec_info_t *n){return decode(o);}
static int esp_opus_dec_decode(void *h,esp_audio_dec_in_raw_t *i,esp_audio_dec_out_frame_t *o,esp_audio_dec_info_t *n){return decode(o);}
static int xSemaphoreTake(int h,int t){clock_us+=lock_us;return lock_fails ? 0 : pdTRUE;}
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
    assert(info_logs==0 && "successful playback must not perform default-level serial logging");
    assert(s_playback_scope.valid && s_playback_scope.generation==7);
    uint32_t waveform_at=s_playback_scope.at_ms;
    starter_media_status_t media=starter_media_status();
    assert(media.audio_played==1 && media.audio_playback_pending==0);
    assert(media.audio_decode_max_us==1200 && media.audio_lock_max_us==700);
    assert(media.audio_pcm_last_samples==320);
    assert(media.audio_decode_avg_us==1200 && media.audio_lock_avg_us==700);
    assert(media.audio_write_max_us==0); /* nonblocking first DMA write */
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
    assert(starter_media_status().audio_write_max_us==20000);
    assert(starter_media_status().audio_write_avg_us>0);
    clock_us+=200000;assert(!starter_media_status().audio_playback_active);
    /* Failures and muted frames must not invent a playback debt. */
    waveform_at=s_playback_scope.at_ms;
    write_fails=true;assert(!play_audio_item(&item));
    assert(s_playback_scope.at_ms==waveform_at);
    assert(!starter_media_status().audio_playback_active);
    write_fails=false;s_speaker_muted=true;assert(!play_audio_item(&item));
    assert(!starter_media_status().audio_playback_active);
    s_speaker_muted=false;
    lock_fails=true;lock_us=500000;
    assert(!play_audio_item(&item));
    assert(starter_media_status().audio_lock_max_us==500000);
    assert(!starter_media_status().audio_playback_active);
    lock_fails=false;lock_us=700;
    item.received_at_ms=(uint32_t)(clock_us/1000)-80;item.arrival_gap_ms=140;
    assert(play_audio_item(&item));
    media=starter_media_status();
    assert(media.audio_trace_frame==media.audio_played);
    assert(media.audio_trace_at_ms>0 && media.audio_trace_write_gap_ms>=60);
    assert(media.audio_trace_rx_gap_ms==140 && media.audio_trace_queue_ms>=80);
    assert(media.audio_trace_decode_us==1200 && media.audio_trace_lock_us==700);
    assert(media.audio_trace_write_us==20000);
    uint32_t captured=media.audio_trace_frame;
    assert(play_audio_item(&item)); /* ordinary pacing preserves the last observation */
    assert(starter_media_status().audio_trace_frame==captured);
    while(starter_media_status().audio_played<100)assert(play_audio_item(&item));
    assert(info_logs==0 && "periodic success summaries also stay off the playback thread at INFO");
    clock_us+=200000;
    item.generation=6;
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
