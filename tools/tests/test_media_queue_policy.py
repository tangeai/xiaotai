"""Replay public ingress under congestion; a held slot must never be reclaimed."""
import unittest
import sys
from pathlib import Path
sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from board_registry import load_boards
from test_s3_product_regressions import ROOT, MEDIA, function
from test_esp_ai_playout import run_media_c
P4=ROOT/'firmware/esp-idf/esp32p4/waveshare-touch-lcd-43c/components/starter_media/src/starter_media.c'

class MediaQueuePolicy(unittest.TestCase):
 def test_registered_audio_boards_reuse_replayed_adapters(self):
  bk=ROOT/'platforms/beken/runtime/src/xiaotai_audio.c'
  replayed={MEDIA.resolve(),P4.resolve(),bk.resolve()}
  for board in load_boards(ROOT):
   if 'audio' not in board.data['capabilities']: continue
   with self.subTest(board=board.id):
    project=ROOT/board.project_dir
    if board.data['platform']=='beken':
     self.assertIn('platforms/beken/runtime/CMakeLists.txt', (project/'ap/CMakeLists.txt').read_text())
     source=bk
    else:
     component=project/'components/starter_media'
     cmake=(component/'CMakeLists.txt').read_text()
     self.assertIn('starter_media_common/CMakeLists.txt',cmake)
     source=(ROOT/'platforms/esp-idf/components/starter_media_s3/starter_media.c'
       if 'starter_media_s3/starter_media.c' in cmake else component/'src/starter_media.c')
    self.assertIn(source.resolve(),replayed,
      'New adapter requires the same AI/live ingress, expiry and ownership replays')
    text=source.read_text()
    self.assertIn('xiaotai_media_policy.h',text)
    self.assertIn('xiaotai_media_overflow(',text)
    self.assertIn('xiaotai_media_expired(',text)

 def test_business_decision_is_independent_of_board_budgets(self):
  from test_s3_product_regressions import run_c
  for age,capacity in ((80,8),(160,64),(240,128)):
   run_c(f'#define XIAOTAI_LIVE_RX_MS {age}U\n'
         f'#define XIAOTAI_AI_PACKET_CAPACITY {capacity}U\n'+r'''#include "xiaotai_media_policy.h"
#include <assert.h>
int main(void){
 assert(xiaotai_media_overflow(true)==XIAOTAI_QUEUE_REJECT_NEW);
 assert(xiaotai_media_overflow(false)==XIAOTAI_QUEUE_REPLACE_OLDEST);
 assert(!xiaotai_media_expired(1000+XIAOTAI_LIVE_RX_MS-1,1000,XIAOTAI_LIVE_RX_MS));
 assert(xiaotai_media_expired(1000+XIAOTAI_LIVE_RX_MS,1000,XIAOTAI_LIVE_RX_MS));
 assert(xiaotai_media_expired(144,0xfffffff0U,160));
 assert(!xiaotai_media_expired(143,0xfffffff0U,160));
}
''', [ROOT/'platforms/common/include'])

 def test_live_ingress_keeps_newest_and_ai_preserves_prefix(self):
  self.replay(False)

 def test_two_producers_and_sink_preserve_slot_ownership(self):
  self.replay(True)

 def replay(self, threaded):
  for path in (MEDIA,P4):
   source=path.read_text()
   helpers=function(source,'update_max_counter')
   if 'static void audio_rx_record_arrival(' in source:
    helpers=function(source,'update_max_counter')+function(source,'audio_rx_record_arrival')
   run_media_c(f'#define RUN_THREADED {int(threaded)}\n'+r'''
#include <assert.h>
#include <string.h>
#include <stdatomic.h>
#include <pthread.h>
#include <sched.h>
/* The test runner may use ptrace, which LSan cannot support. This replay checks
 * ASan/UBSan and fixed-pool accounting; it is not a heap-leak certification. */
const char *__asan_default_options(void){return "detect_leaks=0";}
#define pdTRUE 1
#define STARTER_TIRTC_AI 2
#define AUDIO_RX_QUEUE_DEPTH 64
#define AUDIO_RX_LIVE_QUEUE_DEPTH 8
#define AUDIO_RX_BYTES 1500
#define AUDIO_PACKET_MS 20
#define ESP_LOGW(...) ((void)0)
typedef int starter_tirtc_mode_t;
typedef struct {unsigned length,timestamp_ms;} starter_tirtc_frame_t;
typedef struct {int mode;uint32_t generation;starter_tirtc_frame_t frame;uint32_t received_at_ms,arrival_gap_ms;uint16_t pcm_samples;uint8_t payload[1500];} audio_rx_item_t;
typedef struct {uint8_t slots[64];unsigned head,count;} queue_t;
static queue_t freeq,readyq,*s_audio_rx_free_queue=&freeq,*s_audio_rx_ready_queue=&readyq;
static audio_rx_item_t pool[64],*s_audio_rx_pool=pool;
static atomic_uint s_audio_dropped,s_audio_rx_overflow,s_audio_playback_pending,s_audio_received,s_audio_rx_last_ms;
atomic_uint_fast32_t s_audio_rx_queue_peak,s_audio_rx_burst_max,s_audio_rx_burst_window;
static unsigned now=1000;static bool fail_publish;
static void (*receive_hook)(void);
static int64_t esp_timer_get_time(void){return (int64_t)now*1000;}
static bool same_session(int mode,unsigned generation){return generation==7;}
static pthread_mutex_t queue_lock=PTHREAD_MUTEX_INITIALIZER;
static unsigned uxQueueMessagesWaiting(queue_t*q){pthread_mutex_lock(&queue_lock);unsigned n=q->count;pthread_mutex_unlock(&queue_lock);return n;}
static int xQueueReceive(queue_t*q,uint8_t*v,unsigned timeout){
 if(q==&freeq && receive_hook){void(*hook)(void)=receive_hook;receive_hook=NULL;hook();}
 pthread_mutex_lock(&queue_lock);int ok=q->count!=0;
 if(ok){*v=q->slots[q->head++%64];q->count--;}
 pthread_mutex_unlock(&queue_lock);return ok;
}
static int xQueueSend(queue_t*q,const uint8_t*v,unsigned timeout){
 pthread_mutex_lock(&queue_lock);int ok=q->count<64 && !(q==&readyq&&fail_publish);
 if(ok)q->slots[(q->head+q->count++)%64]=*v;
 pthread_mutex_unlock(&queue_lock);return ok;
}
''' + helpers + function(source,'starter_media_submit_audio') + r'''
static void release_all(void){uint8_t slot;while(xQueueReceive(&readyq,&slot,0)){xQueueSend(&freeq,&slot,0);atomic_fetch_sub(&s_audio_playback_pending,1);}}
static atomic_uint producers_done;
static void *producer(void *arg){
 unsigned base=(unsigned)(uintptr_t)arg*100000;
 for(unsigned i=0;i<20000;i++){
  uint8_t data[160]={0};starter_tirtc_frame_t f={.length=160,.timestamp_ms=base+i};
  memcpy(data,&f.timestamp_ms,sizeof(f.timestamp_ms));
  starter_media_submit_audio(1,7,&f,data);
  assert(s_audio_playback_pending<=8);
 }
 atomic_fetch_add(&producers_done,1);return NULL;
}
static void *sink(void *unused){
 while(producers_done<2 || s_audio_playback_pending){
  uint8_t slot;if(!xQueueReceive(&readyq,&slot,0)){sched_yield();continue;}
  audio_rx_item_t *item=&pool[slot];unsigned stamp=item->frame.timestamp_ms;
  sched_yield(); /* allow producers to try to reclaim while sink holds it */
  assert(item->frame.length==160 && item->frame.timestamp_ms==stamp);
  assert(memcmp(item->payload,&stamp,sizeof(stamp))==0);
  atomic_fetch_sub(&s_audio_playback_pending,1);memset(item,0,sizeof(*item));
  assert(xQueueSend(&freeq,&slot,0));
 }
 return NULL;
}
static void concurrent_arrival(void){uint8_t data[160]={0};starter_tirtc_frame_t f={.length=160,.timestamp_ms=200};starter_media_submit_audio(1,7,&f,data);}
int main(void){
 for(uint8_t i=0;i<64;i++)xQueueSend(&freeq,&i,0);
 uint8_t data[160]={0x08};starter_tirtc_frame_t f={.length=160};
 for(unsigned mode=1;mode<=5;mode++){
  if(mode==2)continue;
  for(unsigned i=0;i<8;i++){f.timestamp_ms=i;starter_media_submit_audio(mode,7,&f,data);}
  uint8_t held;assert(xQueueReceive(&readyq,&held,0));
  f.timestamp_ms=8;starter_media_submit_audio(mode,7,&f,data);
  assert(pool[held].frame.timestamp_ms==0 && readyq.count==7);
  for(unsigned i=2;i<=8;i++){uint8_t slot;assert(xQueueReceive(&readyq,&slot,0));assert(pool[slot].frame.timestamp_ms==i);xQueueSend(&freeq,&slot,0);atomic_fetch_sub(&s_audio_playback_pending,1);}
  xQueueSend(&freeq,&held,0);atomic_fetch_sub(&s_audio_playback_pending,1);
  assert(s_audio_playback_pending==0 && freeq.count==64);
 }
 assert(s_audio_rx_overflow==4 && s_audio_dropped==4);
 fail_publish=true;starter_media_submit_audio(1,7,&f,data);assert(freeq.count==64 && s_audio_playback_pending==0);fail_publish=false;
 /* A second SDK callback preempts the first between capacity check and take. */
 for(unsigned i=0;i<7;i++)starter_media_submit_audio(1,7,&f,data);
 receive_hook=concurrent_arrival;starter_media_submit_audio(1,7,&f,data);
 assert(readyq.count==8 && s_audio_playback_pending==8);
 release_all();assert(freeq.count==64 && s_audio_playback_pending==0);
 /* Failed publication after reclaim must retire exactly one old pending slot. */
 for(unsigned i=0;i<8;i++)starter_media_submit_audio(1,7,&f,data);
 fail_publish=true;starter_media_submit_audio(1,7,&f,data);fail_publish=false;
 assert(readyq.count==7 && s_audio_playback_pending==7 && freeq.count==57);
 release_all();assert(freeq.count==64 && s_audio_playback_pending==0);
 starter_ai_playout_reset(7);
 for(unsigned i=0;i<65;i++){f.timestamp_ms=i;starter_media_submit_audio(2,7,&f,data);}
 assert(readyq.count==64 && s_audio_playback_pending==64);
 for(unsigned i=0;i<64;i++){uint8_t slot;assert(xQueueReceive(&readyq,&slot,0));assert(pool[slot].frame.timestamp_ms==i);atomic_fetch_sub(&s_audio_playback_pending,1);starter_ai_playout_retire(7,pool[slot].pcm_samples);memset(&pool[slot],0,sizeof(pool[slot]));xQueueSend(&freeq,&slot,0);}
 if(RUN_THREADED){
  pthread_t a,b,c;assert(!pthread_create(&c,NULL,sink,NULL));
  assert(!pthread_create(&a,NULL,producer,(void*)1));assert(!pthread_create(&b,NULL,producer,(void*)2));
  assert(!pthread_join(a,NULL));assert(!pthread_join(b,NULL));assert(!pthread_join(c,NULL));
  assert(freeq.count==64 && readyq.count==0 && s_audio_playback_pending==0);
 }
}
''', libraries=['pthread'], threaded=True, sanitize=threaded)

if __name__=='__main__': unittest.main()
