"""Exercise the production H5 connection branch and SDK subscription boundary."""
import unittest
from test_s3_product_regressions import ROOT, function, run_c
from test_s3_voip_audio import COMMON

RUNTIME = ROOT / 'platforms/esp-idf/components/starter_runtime/src/starter_runtime.c'
ADAPTER = ROOT / 'platforms/esp-idf/components/starter_tirtc/src/starter_tirtc.c'

class H5Talkback(unittest.TestCase):
    def test_connection_subscribes_after_playback_ready_and_handles_failure(self):
        source = RUNTIME.read_text()
        start = source.index('    if (event->mode == STARTER_TIRTC_H5 && state == STARTER_RUNTIME_WAITING) {')
        end = source.index('    if (event->mode == STARTER_TIRTC_AI &&', start)
        branch = source[start:end]
        run_c(r'''
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdatomic.h>
#define STARTER_TIRTC_H5 1
#define STARTER_RUNTIME_WAITING 0
#define XIAOTAI_OWNER_STREAM 1
#define ESP_OK 0
#define ESP_ERR_INVALID_STATE -2
static int s_connection_generation, s_session, ready, subscribed, published, ended;
static int start_rc, subscribe_rc;
static atomic_int s_last_error;
static bool xiaotai_runtime_begin(int *s,int owner,bool video){return true;}
static int starter_media_start(int mode,int gen){ready=start_rc==0;return start_rc;}
static int starter_tirtc_subscribe_h5_audio(void){assert(ready);subscribed++;return subscribe_rc;}
static void finish_session(int error){assert(error<0);ended++;ready=0;}
static int session_generation(void){return 1;}
static int xiaotai_runtime_media_started(int *s,int gen){assert(subscribed);return 0;}
static void publish_state(void){published++;}
struct event {int mode,generation;};
static void connected(struct event *event,int state){
''' + branch + r'''
}
int main(void){
 struct event e={1,7};
 connected(&e,0);assert(subscribed==1 && published==1 && !ended);
 subscribe_rc=-9;connected(&e,0);assert(subscribed==2 && published==1 && ended==1);
 start_rc=-2;connected(&e,0);assert(subscribed==2 && ended==2);
 e.mode=4;connected(&e,0);assert(subscribed==2);
}
''')

    def test_h5_uses_downlink_14_and_rejects_other_modes(self):
        source=ADAPTER.read_text()
        body=function(source,'starter_tirtc_subscribe_h5_audio')
        constants='\n'.join(line for line in source.splitlines() if line.startswith('#define H5_DOWN_AUDIO_STREAM '))
        run_c(COMMON+constants+'\n'+body+r'''
int main(void){
 current_mode=STARTER_TIRTC_H5;expected=14;
 assert(starter_tirtc_subscribe_h5_audio()==0 && subscriptions==1);
 subscribe_result=-9;assert(starter_tirtc_subscribe_h5_audio()==-9);
 for(int mode=0;mode<=5;mode++){
  if(mode==STARTER_TIRTC_H5)continue;
  current_mode=mode;unsigned old=subscriptions;
  assert(starter_tirtc_subscribe_h5_audio()<0 && subscriptions==old);
 }
 current_mode=STARTER_TIRTC_H5;s_connection=0;
 assert(starter_tirtc_subscribe_h5_audio()<0);
}
''')

if __name__=='__main__': unittest.main()
