"""Exercise real BK shutdown across partial worker completion and retries."""
import unittest

from test_s3_product_regressions import ROOT, function, run_c


class BkAudioStopRetry(unittest.TestCase):
    def test_partial_worker_join_is_retained_until_remaining_worker_finishes(self):
        source = (ROOT / 'platforms/beken/runtime/src/xiaotai_audio.c').read_text()
        run_c(r'''
#include <assert.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdatomic.h>
#define BK_OK 0
#define BK_FAIL -1
#define BK_ERR_BUSY -4107
#define AUDIO_TASK_STOP_TIMEOUT_MS 1000
#define BK_LOGI(...) ((void)0)
#define BK_LOGE(...) ((void)0)
static bool s_running,s_capture_run,s_play_run;
static bool s_capture_joined,s_play_joined;
static void *s_uplink,*s_uplink_context,*s_opus_encoder,*s_opus_decoder,*s_opus_ring;
static int s_capture_stopped=0,s_play_stopped=1,s_uplink_ns,s_uplink_agc;
static unsigned tokens[2],waits[2],destroyed,deinitialized,stop_requests;
static bool locked,deny_lock;
static bool audio_control_enter(bool wait){assert(wait && !locked);if(deny_lock)return false;locked=true;return true;}
static void audio_control_leave(void){assert(locked);locked=false;}
static int request_stop(void *context){stop_requests++;return 0;}
static struct {void *context;int (*request_stop)(void *);} board={0,request_stop},*s_board_audio=&board;
static int rtos_get_semaphore(int *sem,unsigned timeout){
 assert(timeout==1000);waits[*sem]++;
 if(tokens[*sem]){tokens[*sem]--;return 0;}return -6722;
}
static void rtos_deinit_semaphore(int *sem){deinitialized++;}
static void destroy_devices(void){destroyed++;}
static void opus_encoder_destroy(void *p){assert(0);}
static void opus_decoder_destroy(void *p){assert(0);}
static void psram_free(void *p){assert(0);}
static void aec_destroy(void){}
static void xiaotai_audio_ns_stop(int *p){}
static void xiaotai_audio_agc_stop(int *p){}
static void ring_reset(void){}
''' + function(source, 'audio_stop_locked') + function(source, 'xiaotai_audio_stop') + r'''
int main(void){
 deny_lock=true;
 assert(xiaotai_audio_stop()==BK_ERR_BUSY && !locked && !stop_requests);
 deny_lock=false;
 for(unsigned first=0;first<2;first++){
  s_capture_joined=s_play_joined=false;
  s_running=s_capture_run=s_play_run=true;
  tokens[0]=tokens[1]=waits[0]=waits[1]=0;
  destroyed=deinitialized=stop_requests=0;
  tokens[first]=1;
  assert(xiaotai_audio_stop()==BK_FAIL);
  assert(s_running && !s_capture_run && !s_play_run);
  assert(!destroyed && !deinitialized && waits[first]==1);
  /* Still blocked: do not consume an exited worker's one-shot signal twice. */
  assert(xiaotai_audio_stop()==BK_FAIL);
  assert(waits[first]==1 && !destroyed && !deinitialized);
  tokens[1-first]=1;
  assert(xiaotai_audio_stop()==BK_OK);
  assert(!s_running && destroyed==1 && deinitialized==2);
  assert(waits[first]==1 && waits[1-first]==3);
  unsigned requests=stop_requests;
  assert(xiaotai_audio_stop()==BK_OK);
  assert(destroyed==1 && deinitialized==2 && stop_requests==requests);
 }
}
''')


if __name__ == '__main__':
    unittest.main()
