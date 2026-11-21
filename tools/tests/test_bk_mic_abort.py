"""Replay the build-selected SDK speaker abort and board shutdown request."""
import re
import subprocess
import tempfile
import unittest
from pathlib import Path

from test_s3_product_regressions import ROOT, function, run_c


class BkMicAbort(unittest.TestCase):
    def test_abort_wakes_readers_without_closing_or_freeing_capture(self):
        build = (ROOT/'platforms/beken/tools/build.sh').read_text()
        patches = re.findall(r'"\$support_dir/(tools/patches/bk-avdk-mic-abort-read\.patch)"', build)
        with tempfile.TemporaryDirectory() as temporary:
            sdk = Path(temporary)
            component = sdk/'ap/components/audio_record'
            (component/'include').mkdir(parents=True)
            (component/'src').mkdir()
            fixtures = ROOT/'tools/tests/fixtures'
            header = component/'include/audio_record.h'
            source = component/'src/onboard_mic_record.c'
            header.write_text((fixtures/'bk7258_mic_control.h').read_text())
            source.write_text((fixtures/'bk7258_mic_control.c').read_text())
            for patch in patches:
                subprocess.run(['patch','--batch','-p1','-i',str(ROOT/'platforms/beken'/patch)],
                               cwd=sdk,check=True,capture_output=True)
            board=(ROOT/'platforms/beken/src/xiaotai_onboard_audio.c').read_text()
            run_c(r'''
#include <assert.h>
#include <stdbool.h>
#include <stddef.h>
#define BK_OK 0
#define BK_FAIL -1
#define LOGE(...) ((void)0)
#define ONBOARD_MIC_RECORD_CHECK_NULL(p) do{if(!(p))return BK_FAIL;}while(0)
#define AUDIO_RECORD_STA_PAUSED 1
#define AUDIO_RECORD_STA_RUNNING 2
typedef struct {bool alive,aborted;} ring_buffer;
typedef struct {ring_buffer *rb;} onboard_mic_record_priv_t;
typedef struct {void *record_ctx;struct {int adc_gain;} config;} audio_record_t;
''' + header.read_text() + r'''
static ring_buffer buffer={.alive=true};
static onboard_mic_record_priv_t priv={&buffer};
static audio_record_t mic={&priv,{0}};
static int wakes;
static int rb_abort(ring_buffer *r){assert(r->alive);r->aborted=true;wakes++;return 0;}
static int onboard_mic_state_get(void *p){return 0;}
static void onboard_mic_state_set(void *p,int s){}
static int onboard_mic_stop(void *p){assert(0);return 0;}
static int onboard_mic_start(void *p){assert(0);return 0;}
static int onboard_mic_set_adc_gain(int v){assert(0);return 0;}
''' + source.read_text() + r'''
int main(void){
 for(int i=0;i<100;i++){
  assert(onboard_mic_record_control(&mic,AUDIO_RECORD_ABORT_READ)==0);
  assert(buffer.alive && buffer.aborted && wakes==i+1);
 }
}
''')

    def test_blocked_sdk_reader_resumes_before_storage_is_destroyed(self):
        fixture=(ROOT/'tools/tests/fixtures/bk7258_ring_cancel.c').read_text()
        run_c(r'''
#include <assert.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
typedef void *SemaphoreHandle_t;
typedef int bk_err_t;
typedef unsigned TickType_t;
#define BK_OK 0
#define BK_ERR_ADK_INVALID_ARG -1
#define RB_FAIL -1
#define RB_ABORT -2
#define RB_TIMEOUT -3
#define RB_DONE -4
#define pdTRUE 1
#define portMAX_DELAY 0xffffffffU
#define audio_free free
#define vSemaphoreDelete free
#define os_memcpy memcpy
static void rb_release(void *sem){}
static int rb_block(void *sem,unsigned ticks);
static int xSemaphoreGive(void *sem){return 1;}
'''+fixture+r'''
static ringbuf_handle_t active;
static int rb_block(void *sem,unsigned ticks){
 if(active && sem==active->can_read)assert(rb_abort(active)==0);
 return pdTRUE;
}
int main(void){
 for(unsigned i=0;i<100;i++){
  active=calloc(1,sizeof(*active));active->p_o=malloc(64);active->size=64;
  active->can_read=malloc(1);active->can_write=malloc(1);active->lock=malloc(1);
  char buffer[32];assert(rb_read(active,buffer,sizeof(buffer),portMAX_DELAY)==RB_ABORT);
  assert(active->abort_read && active->abort_write);
  rb_destroy(active);active=NULL;
 }
}
''',cflags=['-fsanitize=address,undefined','-fno-omit-frame-pointer'])

if __name__ == '__main__': unittest.main()
