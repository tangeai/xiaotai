"""Public ESP playout accounting/boundaries with deterministic time and SDK locks."""
from pathlib import Path
import tempfile
import unittest
from test_s3_product_regressions import ROOT, run_c

COMMON = ROOT / 'platforms/esp-idf/components/starter_media_common'


def run_media_c(code, include_dirs=(), libraries=(), profile=2, threaded=False, sanitize=False):
    with tempfile.TemporaryDirectory() as directory:
        fake = Path(directory)
        (fake / 'freertos').mkdir()
        (fake / 'freertos/FreeRTOS.h').write_text('''
#include <assert.h>
typedef int portMUX_TYPE;
#define portMUX_INITIALIZER_UNLOCKED 0
#define portENTER_CRITICAL(p) do {assert(*(p)==0);*(p)=1;} while(0)
#define portEXIT_CRITICAL(p) do {assert(*(p)==1);*(p)=0;} while(0)
''')
        if threaded:
            (fake / 'freertos/FreeRTOS.h').write_text('''#include <pthread.h>
#include <assert.h>
typedef pthread_mutex_t portMUX_TYPE;
#define portMUX_INITIALIZER_UNLOCKED PTHREAD_MUTEX_INITIALIZER
#define portENTER_CRITICAL(p) do {assert(pthread_mutex_lock(p)==0);} while(0)
#define portEXIT_CRITICAL(p) do {assert(pthread_mutex_unlock(p)==0);} while(0)
''')
        (fake / 'freertos/portmacro.h').write_text('')
        run_c(f'#define XIAOTAI_MEDIA_PROFILE {profile}\n' + '#include "xiaotai_media_policy.h"\n#include "starter_ai_playout.h"\n' + code,
              [fake, COMMON / 'include', ROOT / 'platforms/common/include', *include_dirs],
              libraries, [COMMON / 'src/starter_ai_playout.c'],
              cflags=['-fsanitize=address,undefined', '-fno-omit-frame-pointer'] if sanitize else [])


def run_playout(body):
    run_media_c('#include <assert.h>\n#include <stdio.h>\nint main(void){\n' +
                body + '\n}\n')


class ESPAIPlayout(unittest.TestCase):
    def test_single_packet_timeout_and_early_end(self):
        run_playout(r'''
    const uint8_t packet[]={0x08}; /* SILK 20 ms, mono. */
    starter_ai_playout_reset(7);
    assert(starter_ai_playout_admit(7,packet,sizeof(packet))==320);
    assert(!starter_ai_playout_ready(7,1000,1000,0));
    assert(!starter_ai_playout_ready(7,1399,1000,0));
    assert(starter_ai_playout_ready(7,1400,1000,0));
    starter_ai_playout_retire(7,320);
    assert(starter_ai_playout_status().queued_samples==0);
    starter_media_ai_playback_boundary(7,true);
    assert(starter_ai_playout_admit(7,packet,1)==320);
    assert(starter_ai_playout_ready(7,2000,2000,0));
''')


    def test_duration_accounting_stale_generation_and_capacity(self):
        run_playout(r'''
 const uint8_t short_frame[]={0x80}, frame20[]={0x08}, frame60[]={0x18}, frame120[]={0x19};
 const uint8_t invalid[]={0x1b,3}, empty_count[]={0x0b,0};
 starter_ai_playout_reset(7);
 assert(starter_ai_playout_admit(7,short_frame,1)==40);
 assert(starter_ai_playout_admit(7,frame20,1)==320);
 assert(starter_ai_playout_admit(7,frame60,1)==960);
 assert(starter_ai_playout_admit(7,frame120,1)==1920);
 assert(starter_ai_playout_status().queued_samples==3240);
 assert(starter_ai_playout_admit(7,invalid,2)==0);
 assert(starter_ai_playout_admit(7,empty_count,2)==0);
 assert(starter_ai_playout_admit(7,invalid,1)==0);
 assert(starter_ai_playout_admit(7,NULL,0)==0);
 starter_ai_playout_reset(8);starter_ai_playout_admit(8,frame20,1);
 starter_ai_playout_retire(7,1920);starter_media_ai_playback_boundary(7,true);
 starter_ai_playout_written(7,1600);
 assert(starter_ai_playout_status().queued_samples==320);
 assert(!starter_ai_playout_ready(7,1000,1000,0));
 assert(!starter_ai_playout_ready(8,1000,1000,0));
 starter_ai_playout_reset(9);
 for(unsigned i=0;i<64;i++)assert(starter_ai_playout_admit(9,frame120,1)==1920);
 assert(starter_ai_playout_admit(9,frame20,1)==0);
 for(unsigned i=0;i<64;i++)starter_ai_playout_retire(9,1920);
 assert(starter_ai_playout_status().queued_samples==0);
 starter_ai_playout_reset(0);assert(starter_ai_playout_admit(0,frame20,1)==0);
''')

    def test_weak_network_recovery_stability_and_round_boundaries(self):
        run_playout(r'''
 const uint8_t packet[]={0x08};
 starter_ai_playout_reset(7);
 for(unsigned i=0;i<5;i++)starter_ai_playout_admit(7,packet,1);
 assert(starter_ai_playout_ready(7,1000,1000,0));
 starter_ai_playout_retire(7,1600);
 /* Worker resumes on a late packet without polling an empty queue. */
 starter_ai_playout_admit(7,packet,1);
 assert(!starter_ai_playout_ready(7,1150,1150,1100));
 assert(starter_ai_playout_status().target_ms==140);
 assert(starter_ai_playout_status().underruns==1);
 for(unsigned i=0;i<6;i++)starter_ai_playout_admit(7,packet,1);
 assert(starter_ai_playout_ready(7,1200,1150,1100));
 starter_ai_playout_retire(7,2240);
 /* Silence alone must never decay target; successful output does. */
 starter_media_ai_playback_boundary(7,true);
 starter_media_ai_playback_boundary(7,false);
 starter_ai_playout_admit(7,packet,1);
 assert(!starter_ai_playout_ready(7,20000,20000,1200));
 assert(starter_ai_playout_status().target_ms==140);
 starter_media_ai_playback_boundary(7,false); /* duplicate start */
 assert(!starter_ai_playout_ready(7,20399,20000,1200));
 assert(starter_ai_playout_ready(7,20400,20000,1200));
 for(unsigned i=0;i<500;i++)starter_ai_playout_written(7,320);
 assert(starter_ai_playout_status().target_ms==120);
 starter_ai_playout_retire(7,320);
 starter_ai_playout_admit(7,packet,1);
 assert(!starter_ai_playout_ready(7,30000,30000,20420));
 assert(starter_ai_playout_status().target_ms==120); /* ambiguous long gap */
 starter_media_ai_playback_boundary(7,true);
 assert(starter_ai_playout_ready(7,30001,30000,20420));
 assert(starter_ai_playout_status().timeout_releases==1);
''')


def check_prefill(source):
    from test_s3_product_regressions import function
    run_media_c(r'''
#include <assert.h>
#include <stdatomic.h>
#define STARTER_TIRTC_AI 2
#define pdMS_TO_TICKS(x) (x)
typedef struct {int mode;uint32_t generation,received_at_ms;} audio_rx_item_t;
static atomic_bool s_speaker_muted;
static atomic_uint s_audio_dropped;
static uint32_t generation=7,now=1000,cancel_at,mute_at;
static int mode=2;
static bool same_session(int m,uint32_t g){return m==mode && g==generation;}
static int64_t esp_timer_get_time(void){return (int64_t)now*1000;}
static uint32_t audio_playback_tail_ms(void){return 0;}
static void vTaskDelay(unsigned ms){now+=ms;if(cancel_at && now>=cancel_at)generation++;
    if(mute_at && now>=mute_at)s_speaker_muted=true;}
''' + function(source,'audio_rx_prefill') + r'''
int main(void){
 const uint8_t packet[]={0x08};
 audio_rx_item_t item={.mode=2,.generation=7,.received_at_ms=1000};
 starter_ai_playout_reset(7);starter_ai_playout_admit(7,packet,1);
 assert(audio_rx_prefill(&item) && now==1400); /* bounded one-word fallback */
 starter_ai_playout_reset(7);starter_ai_playout_admit(7,packet,1);
 mode=item.mode=1;item.received_at_ms=now-159;assert(audio_rx_prefill(&item));
 item.received_at_ms=now-160;assert(!audio_rx_prefill(&item));
 assert(s_audio_dropped==1);mode=item.mode=2;
 item.received_at_ms=now;cancel_at=now+10;
 assert(!audio_rx_prefill(&item) && now==1410);
 generation=7;cancel_at=0;starter_ai_playout_reset(7);starter_ai_playout_admit(7,packet,1);
 item.received_at_ms=now;mute_at=now+10;
 assert(!audio_rx_prefill(&item) && now==1420);
 s_speaker_muted=false;mute_at=0;starter_media_ai_playback_boundary(7,true);
 assert(audio_rx_prefill(&item) && now==1420); /* explicit end releases tail */
 starter_ai_playout_reset(7);item.received_at_ms=now;
 for(unsigned i=0;i<5;i++)starter_ai_playout_admit(7,packet,1);
 assert(audio_rx_prefill(&item) && now==1420); /* 100 ms ready */
 mode=3;item.mode=3;starter_ai_playout_reset(0);
 assert(audio_rx_prefill(&item) && now==1420); /* live media unchanged */
 generation++;assert(!audio_rx_prefill(&item));
}
''')


if __name__ == '__main__':
    unittest.main()
