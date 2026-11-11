"""Bounded PCM ownership: congestion must retain fresh microphone audio."""
import unittest
from test_s3_product_regressions import ROOT, MEDIA, function, run_c
P4 = ROOT / 'firmware/esp-idf/esp32p4/waveshare-touch-lcd-43c/components/starter_media/src/starter_media.c'

class UplinkFreshness(unittest.TestCase):
    def test_congested_producer_keeps_newest_without_reclaiming_worker(self):
        for source in (MEDIA, P4):
            with self.subTest(source=str(source)):
                run_c(r'''
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include <stdatomic.h>
#define STARTER_TIRTC_AI 2
#define OPUS_PACKET_SAMPLES 320
#define AUDIO_PACKET_SAMPLES 160
#define AUDIO_PACKET_SAMPLES_MAX 320
#define pdTRUE 1
#define pdFALSE 0
typedef int starter_tirtc_mode_t;
typedef struct {int mode;uint32_t generation,timestamp_ms,queued_at_ms;unsigned mute_epoch;size_t sample_count;int16_t pcm[320];} audio_tx_item_t;
typedef struct {uint8_t slots[12];unsigned count,head;} queue_t;
static queue_t free_queue,ready_queue;
static queue_t *s_audio_tx_free_queue=&free_queue,*s_audio_tx_ready_queue=&ready_queue;
static audio_tx_item_t s_audio_tx_pool[12];
static atomic_uint s_audio_dropped,s_mute_epoch;
static atomic_bool s_microphone_muted,s_room_pressed,s_uplink_enabled;
static int64_t esp_timer_get_time(void){return 0;}
static bool enabled=true,fail_publish;
static bool uplink_allowed(int mode){return enabled;}
static int xQueueReceive(queue_t*q,uint8_t*v,unsigned wait){
 if(!q->count){return 0;}*v=q->slots[q->head];q->head=(q->head+1)%12;q->count--;return 1;
}
static int xQueueSend(queue_t*q,const uint8_t*v,unsigned wait){
 if(q==&ready_queue && fail_publish)return 0;
 if(q->count==12){return 0;}q->slots[(q->head+q->count)%12]=*v;q->count++;return 1;
}
''' + function(source.read_text(),'enqueue_uplink_pcm') + function(source.read_text(),'starter_media_set_room_pressed') + function(source.read_text(),'starter_media_set_uplink_enabled') + r'''
int main(void){
 for(uint8_t i=0;i<12;i++)xQueueSend(&free_queue,&i,0);
 int16_t pcm[320]={0};
 /* All audio modes use the same fresh microphone admission, including AI. */
 for(int mode=1;mode<=5;mode++){
  size_t count=mode==2?320:160;
  for(unsigned i=0;i<12;i++){pcm[0]=i;assert(enqueue_uplink_pcm(mode,7,i*20,pcm,count,0));}
  uint8_t held;xQueueReceive(&ready_queue,&held,0); /* worker owns oldest */
  pcm[0]=12;assert(enqueue_uplink_pcm(mode,7,240,pcm,count,0));
  assert(s_audio_tx_pool[held].pcm[0]==0); /* never overwrite worker */
  assert(ready_queue.count==11 && free_queue.count==0);
  for(unsigned i=2;i<=12;i++){
   uint8_t slot;assert(xQueueReceive(&ready_queue,&slot,0));
   assert(s_audio_tx_pool[slot].pcm[0]==i);
   xQueueSend(&free_queue,&slot,0);
  }
  xQueueSend(&free_queue,&held,0);
 }
 assert(s_audio_dropped==5 && free_queue.count==12);
 fail_publish=true;assert(!enqueue_uplink_pcm(3,7,0,pcm,160,0));
 assert(free_queue.count==12 && !ready_queue.count);
 fail_publish=false;s_microphone_muted=true;
 assert(!enqueue_uplink_pcm(3,7,0,pcm,160,0));
 s_microphone_muted=false;s_mute_epoch=1;
 assert(!enqueue_uplink_pcm(3,7,0,pcm,160,0));
 assert(free_queue.count==12);
 s_mute_epoch=0;starter_media_set_room_pressed(true);
 unsigned first_epoch=atomic_load(&s_mute_epoch);
 assert(enqueue_uplink_pcm(5,7,0,pcm,160,first_epoch));
 starter_media_set_room_pressed(false);starter_media_set_room_pressed(true);
 uint8_t old;xQueueReceive(&ready_queue,&old,0);
 assert(s_audio_tx_pool[old].mute_epoch!=atomic_load(&s_mute_epoch));
 assert(!enqueue_uplink_pcm(5,7,20,pcm,160,first_epoch));
 unsigned current=atomic_load(&s_mute_epoch);starter_media_set_room_pressed(true);
 assert(current==atomic_load(&s_mute_epoch)); /* repeated state isn't a new talkspurt */
 xQueueSend(&free_queue,&old,0);assert(free_queue.count==12);
 unsigned previous=s_mute_epoch;starter_media_set_uplink_enabled(true);
 assert(s_mute_epoch==previous+1);starter_media_set_uplink_enabled(false);
 starter_media_set_uplink_enabled(true);assert(s_mute_epoch==previous+3);
 starter_media_set_uplink_enabled(true);assert(s_mute_epoch==previous+3);
}
''')

if __name__ == '__main__':
    unittest.main()
