"""Replay the build-selected SDK speaker abort and board shutdown request."""
import re
import subprocess
import tempfile
import unittest
from pathlib import Path

from test_s3_product_regressions import ROOT, function, run_c


class BkSpeakerAbort(unittest.TestCase):
    def test_stop_request_wakes_writer_without_destroying_the_speaker(self):
        build = (ROOT/'platforms/beken/tools/build.sh').read_text()
        patches = re.findall(r'"\$support_dir/(tools/patches/bk-avdk-speaker-abort-write\.patch)"', build)
        with tempfile.TemporaryDirectory() as temporary:
            sdk = Path(temporary)
            component = sdk/'ap/components/audio_play'
            (component/'include').mkdir(parents=True)
            (component/'src').mkdir()
            fixtures = ROOT/'tools/tests/fixtures'
            header = component/'include/audio_play.h'
            source = component/'src/onboard_speaker_play.c'
            header.write_text((fixtures/'bk7258_speaker_control.h').read_text())
            source.write_text((fixtures/'bk7258_speaker_control.c').read_text())
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
#define RB_OK 0
#define RB_FAIL -1
#define LOGE(...) ((void)0)
#define ONBOARD_SPK_PLAY_CHECK_NULL(p) do{if(!(p))return BK_FAIL;}while(0)
#define AUDIO_PLAY_STA_PAUSED 1
#define AUDIO_PLAY_STA_RUNNING 2
typedef int bk_err_t;
typedef struct {bool abort_write;int can_write;} ring_buffer;
typedef ring_buffer *ringbuf_handle_t;
typedef struct {ringbuf_handle_t rb;} onboard_speaker_play_priv_t;
typedef struct {void *play_ctx;struct {int volume;} config;} audio_play_t;
''' + header.read_text() + r'''
static ring_buffer buffer;
static onboard_speaker_play_priv_t priv={&buffer};
static audio_play_t speaker={&priv,{0}},*s_play=&speaker;
static int mic,*s_record=&mic,wakes,mic_closes;
static int xSemaphoreGive(int sem){assert(sem==buffer.can_write);wakes++;return 1;}
static int onboard_speaker_state_get(void *p){return 0;}
static void onboard_speaker_state_set(void *p,int state){}
static int onboard_speaker_stop(void *p){assert(0);return 0;}
static int onboard_speaker_start(void *p){assert(0);return 0;}
static int onboard_speaker_mute(void){assert(0);return 0;}
static int onboard_speaker_unmute(void){assert(0);return 0;}
static int onboard_speaker_set_volume(int v){assert(0);return 0;}
static int xiaotai_board_amplifier_set_playing(bool playing){assert(!playing);return 0;}
static int audio_record_close(int *p){assert(p==&mic);mic_closes++;return 0;}
''' + (fixtures/'bk7258_speaker_abort.c').read_text() + source.read_text() + r'''
static int audio_play_control(audio_play_t *play,audio_play_ctl_t command){return onboard_speaker_play_control(play,command);}
''' + function(board,'audio_adapter_request_stop') + r'''
int main(void){
 assert(audio_adapter_request_stop(NULL)==BK_OK);
 assert(buffer.abort_write && wakes==1 && mic_closes==1);
 /* The reader/writer still own their objects until the runtime joins them. */
 assert(s_play==&speaker && s_play->play_ctx==&priv && priv.rb==&buffer);
 assert(audio_adapter_request_stop(NULL)==BK_OK);
 assert(wakes==2 && mic_closes==2);
 s_play=NULL;s_record=NULL;
 assert(audio_adapter_request_stop(NULL)==BK_OK && wakes==2);
}
''')


if __name__ == '__main__':
    unittest.main()
