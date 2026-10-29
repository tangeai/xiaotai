"""Exercise isolated recognition playback through its actual output port."""
import pathlib
import subprocess
import tempfile
import unittest

ROOT=pathlib.Path(__file__).resolve().parents[2]
COMMON=ROOT/'platforms/esp-idf/components/starter_media_common'

class RecognitionPCMPlayerTest(unittest.TestCase):
    def test_exact_16k_output_cancel_and_failure_cleanup(self):
        source=r'''
#include "starter_recognition_player.h"
#include <assert.h>
static starter_recognition_player_t player;
static int chunks, frames, ended, fail, cancel_after, begin_fail;
static int begin(void *ctx, unsigned epoch) {
    (void)ctx; (void)epoch; assert(starter_recognition_player_busy(&player));
    return begin_fail;
}
static int write_pcm(void *ctx,const int16_t *stereo,size_t count,unsigned epoch) {
    (void)ctx; assert(epoch==starter_recognition_player_epoch(&player));
    assert(count<=320); chunks++;
    for(size_t i=0;i<count;i++) {
        assert(stereo[i*2]==(int16_t)(frames+i));
        assert(stereo[i*2+1]==stereo[i*2]);
    }
    frames+=(int)count;
    if(cancel_after && chunks==cancel_after) starter_recognition_player_cancel(&player);
    return fail;
}
static int end(void *ctx) { (void)ctx; ended++; return 0; }
int main(void) {
    int16_t pcm[16000];for(int i=0;i<16000;i++) pcm[i]=(int16_t)i;
    starter_recognition_output_t out={.begin=begin,.write=write_pcm,.end=end};
    unsigned epoch=starter_recognition_player_epoch(&player);
    assert(starter_recognition_player_play(&player,&out,pcm,16000,epoch)==0);
    assert(frames==16000 && chunks==50 && ended==1);
    assert(!starter_recognition_player_busy(&player));
    chunks=frames=0; cancel_after=1;
    assert(starter_recognition_player_play(&player,&out,pcm,16000,epoch)!=0);
    assert(frames==320 && ended==2);
    assert(starter_recognition_player_play(&player,&out,pcm,16000,epoch)!=0);
    assert(ended==2); /* stale admission must never acquire output */
    epoch=starter_recognition_player_epoch(&player);
    cancel_after=0; fail=42; chunks=frames=0;
    assert(starter_recognition_player_play(&player,&out,pcm,16000,epoch)==42);
    assert(chunks==1 && ended==3 && !starter_recognition_player_busy(&player));
    fail=0;begin_fail=43;
    assert(starter_recognition_player_play(&player,&out,pcm,16000,epoch)==43);
    assert(ended==3 && !starter_recognition_player_busy(&player));
}
'''
        with tempfile.TemporaryDirectory() as tmp:
            p=pathlib.Path(tmp); (p/'test.c').write_text(source)
            subprocess.run(['cc','-std=c11','-Wall','-Wextra','-Werror',
                '-I'+str(COMMON/'include'),str(p/'test.c'),
                str(COMMON/'src/starter_recognition_player.c'),'-o',str(p/'test')],check=True)
            subprocess.run([str(p/'test')],check=True)

    def test_worker_and_boards_use_dedicated_lane(self):
        worker=(ROOT/'platforms/esp-idf/components/starter_runtime/src/starter_recognition.c').read_text()
        self.assertIn('starter_media_play_recognition_pcm16k',worker)
        self.assertNotIn('starter_media_play_pcm8k',worker)
        self.assertIn('xiaotai_recognition_wav_parse',worker)
        for path in ('platforms/esp-idf/components/starter_media_s3/starter_media.c',
                     'firmware/esp-idf/esp32p4/waveshare-touch-lcd-43c/components/starter_media/src/starter_media.c'):
            board=(ROOT/path).read_text()
            self.assertIn('starter_recognition_player_play(&s_recognition_player',board)
            self.assertIn('starter_media_cancel_recognition_playback();',board)
            self.assertIn('starter_recognition_player_busy(&s_recognition_player)',board)

    def test_real_board_ports_allow_volume_and_preserve_new_owner(self):
        for relative in ('platforms/esp-idf/components/starter_media_s3/starter_media.c',
                         'firmware/esp-idf/esp32p4/waveshare-touch-lcd-43c/components/starter_media/src/starter_media.c'):
            source=(ROOT/relative).read_text()
            port=source[source.index('/* Recognition-only output port.'):source.index('esp_err_t starter_media_play_pcm8k_at_epoch(')]
            start=source.index('esp_err_t starter_media_set_speaker_volume(')
            volume=source[start:source.index('esp_err_t starter_media_set_speaker_muted(',start)]
            harness=r'''
#include "starter_recognition_player.h"
#include <assert.h>
typedef int esp_err_t;
#define ESP_OK 0
#define ESP_FAIL 1
#define ESP_ERR_INVALID_STATE 2
#define ESP_ERR_TIMEOUT 3
#define ESP_ERR_INVALID_ARG 4
#define ESP_ERR_NOT_SUPPORTED 5
#define pdTRUE 1
#define portMAX_DELAY 0xffffffffU
#define pdMS_TO_TICKS(x) (x)
static atomic_bool s_ready=true,s_active,s_speaker_muted,s_prompt_active;
static atomic_uchar s_speaker_volume=7;
static atomic_int_fast64_t s_prompt_guard_ms;
static starter_recognition_player_t s_recognition_player;
static void *s_audio_output_mutex=(void *)1,*s_speaker_dev=(void *)1;
static bool locked,amp,takeover;
static int blocks,volume_changes;static int64_t clock_us;
static int xSemaphoreTake(void *m,unsigned timeout) {(void)m;(void)timeout;if(locked)return 0;locked=true;return 1;}
static void xSemaphoreGive(void *m) {(void)m;assert(locked);locked=false;}
static int64_t esp_timer_get_time(void){return clock_us;}
static int audio_output_set_locked(bool enabled){assert(locked);amp=enabled;return 0;}
static bool ready(void *c){(void)c;return true;}
static int set_volume(void *c,unsigned value){(void)c;assert(locked && value==80);volume_changes++;return 0;}
static int write_pcm(void *c,const int16_t *pcm,size_t frames,size_t *written){
 (void)c;assert(locked && frames<=320);blocks++;
 for(size_t i=0;i<frames*2;i++) assert(pcm[i]==42);
 *written=frames*4;return 0;
}
static struct {void *context;bool (*ready)(void *);int (*set_volume)(void *,unsigned);
 int (*write_pcm)(void *,const int16_t *,size_t,size_t *);} adapter={0,ready,set_volume,write_pcm},*s_audio_adapter=&adapter;
esp_err_t starter_media_set_speaker_volume(uint8_t);
void starter_media_cancel_recognition_playback(void);
static void vTaskDelay(unsigned ms){
 clock_us+=ms*1000;
 if(ms==1){assert(!locked);assert(starter_media_set_speaker_volume(8)==0);
   if(takeover){atomic_store(&s_active,true);starter_media_cancel_recognition_playback();amp=true;}}
}
'''+volume+port+r'''
int main(void){
 (void)s_speaker_dev;
 int16_t pcm[1600];for(int i=0;i<1600;i++)pcm[i]=42;
 assert(starter_media_play_recognition_pcm16k(pcm,1600,0)==0);
 assert(blocks==5 && volume_changes==5 && !amp && !locked);
 assert(atomic_load(&s_prompt_guard_ms)>=1000);
 atomic_store(&s_prompt_active,true);
 assert(starter_media_play_recognition_pcm16k(pcm,1600,0)!=0);
 assert(blocks==5);atomic_store(&s_prompt_active,false);
 blocks=0;takeover=true;
 assert(starter_media_play_recognition_pcm16k(pcm,1600,0)!=0);
 assert(blocks==1 && amp && !locked); /* cleanup cannot mute the new call */
 assert(!starter_recognition_player_busy(&s_recognition_player));
}
'''
            with self.subTest(board=relative), tempfile.TemporaryDirectory() as tmp:
                path=pathlib.Path(tmp);(path/'test.c').write_text(harness)
                subprocess.run(['cc','-std=c11','-Wall','-Wextra','-Werror',
                    '-I'+str(COMMON/'include'),str(path/'test.c'),
                    str(COMMON/'src/starter_recognition_player.c'),'-o',str(path/'test')],check=True)
                subprocess.run([str(path/'test')],check=True)

if __name__=='__main__': unittest.main()
