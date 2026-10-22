"""Replay prompt ownership/repetition through the BK production API."""
import unittest
from test_s3_product_regressions import ROOT, function, run_c
BASE = ROOT / 'platforms/beken/runtime'

class BkAcknowledgement(unittest.TestCase):
    def replay(self, scenario):
        source=(BASE/'src/xiaotai_audio.c').read_text()
        # Missing entry point is a feature regression before implementation.
        preview=function(source,'xiaotai_audio_preview_acknowledgement')
        run_c(r'''
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <stdatomic.h>
#define BK_OK 0
#define BK_FAIL -1
#define BK_ERR_PARAM -1
#define BK_ERR_BUSY -2
#define BK_ERR_NO_MEM -3
#define AUDIO_TASK_PRIORITY 1
#define AUDIO_TASK_STACK 4096
#define AUDIO_TASK_STOP_TIMEOUT_MS 1000
#define AUDIO_MAX_FRAME_SAMPLES 160
#define BK_LOGE(...) ((void)0)
#define BK_LOGI(...) ((void)0)
typedef uintptr_t beken_thread_arg_t;
static bool s_running,s_prompt_sync_ready,s_speaker_muted;
static atomic_bool s_prompt_active,s_prompt_run;
static int s_prompt_stopped;
static void *s_prompt_thread;
static unsigned s_prompt_repeats;
static atomic_int s_prompt_result;
static int16_t *s_prompt_pcm;
static size_t s_prompt_samples;
static const int16_t xiaotai_ack_female[2400]={11};
static const int16_t xiaotai_ack_male[2400]={22};
static int allocation_fail,task_fail,board_fail,writes,freed,opened;
static void (*worker)(beken_thread_arg_t);
static int semaphore_fail,write_fail_at,cancel_at,stopped,allocations;
void xiaotai_audio_cancel_prompt(void);
static void *psram_malloc(size_t n){if(allocation_fail)return NULL;void *p=malloc(n);if(p)allocations++;return p;}
static void psram_free(void *p){assert(p && allocations>0);allocations--;freed++;free(p);}
static int rtos_init_semaphore(int *s,int n){return semaphore_fail?-1:0;}
static int rtos_get_semaphore(int *s,int t){return t>0?-1:0;}
static int rtos_set_semaphore(int *s){return 0;}
static int rtos_create_psram_thread(void **t,int pr,const char*n,void(*f)(beken_thread_arg_t),int st,void*a){
 if(task_fail)return -1;
 *t=(void*)1;worker=f;return 0;
}
static void rtos_delay_milliseconds(unsigned n){}
static void rtos_delete_thread(void *t){}
static int xiaotai_board_prompt_audio_start(void){opened++;return board_fail?-1:0;}
static int xiaotai_board_prompt_audio_write(const int16_t *p,size_t n){
 writes++;if(writes==cancel_at)xiaotai_audio_cancel_prompt();
 return writes==write_fail_at?-1:(int)(n*sizeof(*p));
}
static void xiaotai_board_prompt_audio_stop(void){stopped++;}
''' + function(source,'play_prompt_pcm') + function(source,'prompt_task') +
              function(source,'xiaotai_audio_prompt_result') + function(source,'xiaotai_audio_cancel_prompt') + function(source,'start_prompt') + function(source,'xiaotai_audio_announce_verification_pcm') + preview + r'''
int main(void){
''' + scenario + r'''
}
''')

    def test_preview_is_one_shot_and_does_not_steal_live_audio(self):
        self.replay(r'''
 s_running=true;assert(xiaotai_audio_preview_acknowledgement(false)!=0 && !s_prompt_active);
 s_running=false;s_speaker_muted=true;assert(xiaotai_audio_preview_acknowledgement(false)!=0);
 s_speaker_muted=false;allocation_fail=1;assert(xiaotai_audio_preview_acknowledgement(false)!=0);
 allocation_fail=0;task_fail=1;assert(xiaotai_audio_preview_acknowledgement(false)!=0 && freed==1 && !s_prompt_active);
 task_fail=0;assert(xiaotai_audio_preview_acknowledgement(false)==0);
 assert(s_prompt_pcm[0]==11 && s_prompt_repeats==1);
 assert(xiaotai_audio_preview_acknowledgement(true)!=0);
 worker(0);assert(writes==15 && !s_prompt_active && !s_prompt_pcm);
 assert(xiaotai_audio_preview_acknowledgement(true)==0 && s_prompt_pcm[0]==22);
 worker(0);assert(writes==30);
 int16_t *pcm=psram_malloc(2400*sizeof(*pcm));memset(pcm,0,2400*sizeof(*pcm));
 assert(xiaotai_audio_announce_verification_pcm(pcm,2400)==0 && s_prompt_repeats==3);
 worker(0);assert(writes==75 && !s_prompt_active);
 assert(opened==3);
 int result;board_fail=1;assert(xiaotai_audio_preview_acknowledgement(false)==0);
 assert(!xiaotai_audio_prompt_result(&result));worker(0);
 assert(xiaotai_audio_prompt_result(&result) && result!=0 && !s_prompt_pcm);
 assert(allocations==0);
''')

    def test_write_failure_at_every_chunk_releases_and_allows_retry(self):
        self.replay(r'''
 for(int chunk=1;chunk<=15;chunk++){
  writes=0;write_fail_at=chunk;int before=freed,stops=stopped,result;
  assert(xiaotai_audio_preview_acknowledgement(false)==0);
  assert(!xiaotai_audio_prompt_result(&result));worker(0);
  assert(writes==chunk && freed==before+1 && stopped==stops+1);
  assert(allocations==0 && !s_prompt_pcm && !s_prompt_active && !s_prompt_thread);
  assert(xiaotai_audio_prompt_result(&result) && result!=BK_OK);
  writes=0;write_fail_at=0;
  assert(xiaotai_audio_preview_acknowledgement(true)==0);worker(0);
  assert(writes==15 && allocations==0 && s_prompt_samples==0);
  assert(xiaotai_audio_prompt_result(&result) && result==BK_OK);
 }
''')

    def test_cancel_before_and_during_playback_keeps_ownership_until_exit(self):
        self.replay(r'''
 for(int chunk=0;chunk<=15;chunk++){
  writes=0;cancel_at=chunk;
  int before=freed,stops=stopped;
  assert(xiaotai_audio_preview_acknowledgement(false)==0);
  if(chunk==0){
   // The RTOS wait may time out; buffer ownership stays with the worker.
   xiaotai_audio_cancel_prompt();
   assert(s_prompt_active && allocations==1 && freed==before);
   assert(xiaotai_audio_preview_acknowledgement(true)==BK_ERR_BUSY);
  }
  worker(0);
  assert(writes==chunk && stopped==stops+1 && freed==before+1);
  assert(!s_prompt_active && !s_prompt_run && !s_prompt_pcm && allocations==0);
  xiaotai_audio_cancel_prompt();assert(freed==before+1);
  cancel_at=0;writes=0;
  assert(xiaotai_audio_preview_acknowledgement(true)==0);worker(0);
  assert(writes==15 && allocations==0);
 }
''')

    def test_semaphore_failure_returns_buffer_and_retry_succeeds(self):
        self.replay(r'''
 semaphore_fail=1;
 assert(xiaotai_audio_preview_acknowledgement(false)!=0);
 assert(allocations==0 && freed==1 && !s_prompt_sync_ready && !s_prompt_active);
 semaphore_fail=0;
 assert(xiaotai_audio_preview_acknowledgement(false)==0);worker(0);
 assert(allocations==0 && writes==15 && s_prompt_sync_ready);
''')

if __name__=='__main__':unittest.main()
