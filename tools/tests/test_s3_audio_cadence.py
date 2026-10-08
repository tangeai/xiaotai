"""Replay bursty network arrival through the production S3 RX queue/sink."""
import re
import unittest
from test_s3_product_regressions import MEDIA, function, run_c


class S3AudioCadence(unittest.TestCase):
    def replay(self, burst, alternating, startup=False):
        source = MEDIA.read_text()
        depth = re.search(r'#define AUDIO_RX_QUEUE_DEPTH (\d+)U', source)[1]
        helpers = ''
        if 'static bool audio_rx_prefill(' in source:
            helpers = function(source, 'audio_rx_prefill')
        ingress_helpers = ''
        if 'static void audio_rx_record_arrival(' in source:
            ingress_helpers = (function(source, 'update_max_counter') +
                               function(source, 'audio_rx_record_arrival'))
        constants = '\n'.join(line for line in source.splitlines()
                              if line.startswith(('#define AUDIO_RX_PREFILL_', '#define AUDIO_RX_LIVE_')))
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
#define AUDIO_RX_BYTES 1500U
#define AUDIO_PACKET_MS 20U
#define AUDIO_PLAYBACK_IDLE_MS 60U
#define STARTER_TIRTC_AI 2
#define AUDIO_RX_QUEUE_DEPTH ''' + depth + '\n' + constants + r'''
typedef unsigned TickType_t;
typedef int starter_tirtc_mode_t;
typedef struct {size_t length;uint32_t timestamp_ms;} starter_tirtc_frame_t;
typedef struct {int mode;uint32_t generation;starter_tirtc_frame_t frame;
    uint8_t payload[AUDIO_RX_BYTES];} audio_rx_item_t;
typedef struct {uint8_t slots[64];unsigned head,count;} queue_t;
static queue_t ready,free_slots;
static queue_t *s_audio_rx_ready_queue=&ready,*s_audio_rx_free_queue=&free_slots;
static audio_rx_item_t pool[AUDIO_RX_QUEUE_DEPTH],*s_audio_rx_pool=pool;
static atomic_uint s_audio_dropped,s_audio_received;
static atomic_uint s_audio_rx_overflow,s_audio_rx_prefill_waits;
static atomic_uint_fast32_t s_audio_rx_burst_window,s_audio_rx_burst_max,
    s_audio_rx_queue_peak;
static unsigned now_ms,next_packet,played,last_end,gaps;
static unsigned arrivals[120];
static jmp_buf done;
static bool same_session(int mode,uint32_t generation){return mode==2 && generation==7;}
static int64_t esp_timer_get_time(void){return (int64_t)now_ms*1000;}
static unsigned uxQueueMessagesWaiting(queue_t *q){return q->count;}
static int xQueueSend(queue_t *q,const uint8_t *v,unsigned timeout){
    if(q->count==AUDIO_RX_QUEUE_DEPTH)return pdFALSE;
    q->slots[(q->head+q->count)%64]=*v;q->count++;return pdTRUE;
}
static void pump(void);
static void advance(unsigned target){
    while(next_packet<120 && arrivals[next_packet]<=target){
        now_ms=arrivals[next_packet];pump();
    }
    now_ms=target;
}
static void vTaskDelay(unsigned ticks){advance(now_ms+ticks);}
static int xQueueReceive(queue_t *q,uint8_t *v,unsigned timeout){
    if(!q->count && timeout){
        if(timeout==portMAX_DELAY){
            if(next_packet==120)longjmp(done,1);
            advance(arrivals[next_packet]);
        }else{
            unsigned target=now_ms+timeout;
            if(next_packet<120 && arrivals[next_packet]<target)target=arrivals[next_packet];
            advance(target);
        }
    }
    if(!q->count)return pdFALSE;
    *v=q->slots[q->head];q->head=(q->head+1)%64;q->count--;return pdTRUE;
}
''' + ingress_helpers + function(source, 'starter_media_submit_audio') + r'''
static void pump(void){
    while(next_packet<120 && arrivals[next_packet]<=now_ms){
        starter_tirtc_frame_t frame={.length=31,.timestamp_ms=next_packet*20};
        uint8_t payload[31]={0};next_packet++;
        starter_media_submit_audio(2,7,&frame,payload);
    }
}
static bool play_audio_item(const audio_rx_item_t *item){
    if(played && now_ms>last_end)gaps+=now_ms-last_end;
    assert(item->frame.timestamp_ms==played*20);
    assert(now_ms<=item->frame.timestamp_ms+1280); /* bounded queued latency */
    played++;advance(now_ms+20);last_end=now_ms;return true;
}
''' + helpers + function(source, 'audio_sink_task') + r'''
int main(void){
    for(unsigned i=0;i<AUDIO_RX_QUEUE_DEPTH;i++){uint8_t slot=i;xQueueSend(&free_slots,&slot,0);}
''' + f'''
    for(unsigned i=0;i<120;i++){{
        unsigned group=i/{burst}; (void)group;
        arrivals[i]={'i<56 ? (i/7)*10 : 80+(i-56)*20' if startup else '(group/2)*120+(group%2)*100' if alternating else f'group*{burst * 20}'};
    }}
''' + r'''
    pump();if(!setjmp(done))audio_sink_task(NULL);
    assert(atomic_load(&s_audio_dropped)==0 && "RX capacity must retain burst packets");
    assert(played==120 && "every 20ms speech frame must reach I2S");
    assert(gaps==0 && "jitter must not stretch speech with recurring playback gaps");
    assert(free_slots.count==AUDIO_RX_QUEUE_DEPTH);
''' + f'    assert(atomic_load(&s_audio_rx_burst_max)=={7 if startup else burst});\n' + r'''
}
''')

    def test_jitter_does_not_stretch_speech(self):
        self.replay(3, True)

    def test_twelve_packet_burst_preserves_speech(self):
        self.replay(12, False)

    def test_startup_burst_then_realtime_speech_does_not_lose_words(self):
        # Model the observed pattern: <=7 arrivals/10ms, followed by 50fps.
        # Aggregate status does not provide exact original arrival times; this
        # is a bounded stress replay, not a claim to reproduce the live trace.
        self.replay(7, False, startup=True)

    def test_queue_is_bounded_and_owner_switch_discards_ai_backlog(self):
        source = MEDIA.read_text()
        constants = '\n'.join(line for line in source.splitlines()
                              if line.startswith(('#define AUDIO_RX_QUEUE_', '#define AUDIO_RX_LIVE_')))
        run_c(r'''
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include <stdatomic.h>
#define pdTRUE 1
#define pdFALSE 0
#define STARTER_TIRTC_AI 2
#define AUDIO_RX_BYTES 1500U
''' + constants + r'''
typedef int starter_tirtc_mode_t;
typedef struct {size_t length;uint32_t timestamp_ms;} starter_tirtc_frame_t;
typedef struct {int mode;uint32_t generation;starter_tirtc_frame_t frame;
    uint8_t payload[AUDIO_RX_BYTES];} audio_rx_item_t;
typedef struct {uint8_t slots[64];unsigned head,count;} queue_t;
static queue_t ready,free_slots;
static queue_t *s_audio_rx_ready_queue=&ready,*s_audio_rx_free_queue=&free_slots;
static audio_rx_item_t pool[AUDIO_RX_QUEUE_DEPTH],*s_audio_rx_pool=pool;
static atomic_uint s_audio_dropped,s_audio_received,s_audio_rx_overflow,
    s_audio_rx_queue_peak;
static int active_mode=2;
static uint32_t active_generation=7;
static bool same_session(int m,uint32_t g){return m==active_mode && g==active_generation;}
static unsigned uxQueueMessagesWaiting(queue_t *q){return q->count;}
static int xQueueSend(queue_t *q,const uint8_t *v,unsigned timeout){
    if(q->count==AUDIO_RX_QUEUE_DEPTH)return pdFALSE;
    q->slots[(q->head+q->count)%64]=*v;q->count++;return pdTRUE;
}
static int xQueueReceive(queue_t *q,uint8_t *v,unsigned timeout){
    if(!q->count)return pdFALSE;
    *v=q->slots[q->head];q->head=(q->head+1)%64;q->count--;return pdTRUE;
}
static void audio_rx_record_arrival(void){}
static void update_max_counter(atomic_uint *p,unsigned v){if(v>*p)*p=v;}
''' + function(source, 'starter_media_submit_audio') + function(source, 'drain_audio_rx_ready_queue') + r'''
int main(void){
    for(unsigned i=0;i<AUDIO_RX_QUEUE_DEPTH;i++){uint8_t slot=i;xQueueSend(&free_slots,&slot,0);}
    starter_tirtc_frame_t frame={.length=31};uint8_t data[31]={0};
    for(unsigned i=0;i<80;i++)starter_media_submit_audio(2,7,&frame,data);
    assert(ready.count==64 && s_audio_received==64 && s_audio_rx_overflow==16);
    active_generation++;active_mode=3;
    drain_audio_rx_ready_queue();
    assert(ready.count==0 && free_slots.count==AUDIO_RX_QUEUE_DEPTH);
    for(unsigned i=0;i<AUDIO_RX_QUEUE_DEPTH;i++)assert(pool[i].frame.length==0);
    starter_media_submit_audio(2,7,&frame,data); /* old AI callback */
    assert(ready.count==0);
    for(unsigned i=0;i<25;i++)starter_media_submit_audio(3,8,&frame,data);
    assert(ready.count==24 && s_audio_rx_overflow==17); /* VoIP budget unchanged */
    drain_audio_rx_ready_queue();assert(free_slots.count==AUDIO_RX_QUEUE_DEPTH);
}
''')

    def test_prefill_is_bounded_and_stops_with_the_session(self):
        source = MEDIA.read_text()
        constants = '\n'.join(line for line in source.splitlines()
                              if line.startswith(('#define AUDIO_RX_PREFILL_', '#define AUDIO_RX_LIVE_')))
        run_c(r'''
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdatomic.h>
#define STARTER_TIRTC_AI 2
#define pdMS_TO_TICKS(x) (x)
''' + constants + r'''
typedef struct {int mode;uint32_t generation;} audio_rx_item_t;
static unsigned now_ms,queued,stop_at;
static uint32_t generation=7;
static int s_audio_rx_ready_queue;
static atomic_uint s_audio_rx_prefill_waits;
static bool same_session(int mode,uint32_t g){return generation==g;}
static int64_t esp_timer_get_time(void){return (int64_t)now_ms*1000;}
static unsigned uxQueueMessagesWaiting(int queue){return queued;}
static void vTaskDelay(unsigned ms){now_ms+=ms;if(stop_at && now_ms>=stop_at)generation++;}
''' + function(source, 'audio_rx_prefill') + r'''
int main(void){
    audio_rx_item_t first={.mode=2,.generation=7};
    assert(audio_rx_prefill(&first));
    assert(now_ms<=80 && now_ms>0); /* A one-packet reply must eventually play. */
    now_ms=0;stop_at=6;assert(!audio_rx_prefill(&first) && now_ms==6);
    now_ms=0;assert(!audio_rx_prefill(&first) && now_ms==0); /* stale */
    first.generation=generation;queued=3;assert(audio_rx_prefill(&first) && now_ms==0);
    first.mode=1;queued=0;assert(audio_rx_prefill(&first) && now_ms==0); /* H5 */
    assert(atomic_load(&s_audio_rx_prefill_waits)==2);
}
''')


if __name__ == '__main__':
    unittest.main()
