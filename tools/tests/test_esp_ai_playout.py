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
    def test_full_short_packet_queue_can_recover_before_timeout(self):
        run_playout(r'''
 const uint8_t normal[] = {0x08}, shortest[] = {0x80};
 starter_ai_playout_reset(7);
 for(unsigned gap=0; gap<2; ++gap) {
     unsigned start=1000+gap*500;
     for(unsigned i=0; i<12; ++i) assert(starter_ai_playout_admit(7,normal,1)==320);
     assert(starter_ai_playout_ready(7,start,start,start-100));
     for(unsigned i=0; i<12; ++i) starter_ai_playout_retire(7,320);
 }
 /* The next gap raises the target to 280 ms. A full pool of 2.5 ms
  * packets only contains 160 ms. The worker must start freeing slots. */
 for(unsigned i=0; i<63; ++i) assert(starter_ai_playout_admit(7,shortest,1)==40);
 assert(!starter_ai_playout_ready(7,2000,2000,1900));
 assert(starter_ai_playout_admit(7,shortest,1)==40);
 assert(starter_ai_playout_ready(7,2001,2000,1900));
 assert(starter_ai_playout_status().timeout_releases==0);
 for(unsigned i=0; i<64; ++i) starter_ai_playout_retire(7,40);
 assert(starter_ai_playout_status().queued_samples==0);
''')

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
 starter_ai_playout_written(7,1600,1000);
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
 for(unsigned i=0;i<10;i++)starter_ai_playout_admit(7,packet,1);
 assert(starter_ai_playout_ready(7,1000,1000,0));
 for(unsigned i=0;i<10;i++) starter_ai_playout_retire(7,320);
 /* Worker resumes on a late packet without polling an empty queue. */
 starter_ai_playout_admit(7,packet,1);
 assert(!starter_ai_playout_ready(7,1150,1150,1100));
 assert(starter_ai_playout_status().target_ms==240);
 assert(starter_ai_playout_status().underruns==1);
 for(unsigned i=0;i<11;i++)starter_ai_playout_admit(7,packet,1);
 assert(starter_ai_playout_ready(7,1200,1150,1100));
 for(unsigned i=0;i<12;i++) starter_ai_playout_retire(7,320);
 /* Silence alone must never decay target; successful output does. */
 starter_media_ai_playback_boundary(7,true);
 starter_media_ai_playback_boundary(7,false);
 starter_ai_playout_admit(7,packet,1);
 assert(!starter_ai_playout_ready(7,20000,20000,1200));
 assert(starter_ai_playout_status().target_ms==240);
 starter_media_ai_playback_boundary(7,false); /* duplicate start */
 assert(!starter_ai_playout_ready(7,20399,20000,1200));
 assert(starter_ai_playout_ready(7,20400,20000,1200));
 for(unsigned i=0;i<500;i++)starter_ai_playout_written(7,320,20400+i*20);
 assert(starter_ai_playout_status().target_ms==220);
 starter_ai_playout_retire(7,320);
 starter_ai_playout_admit(7,packet,1);
 assert(!starter_ai_playout_ready(7,32000,32000,30400));
 assert(starter_ai_playout_status().target_ms==220); /* ambiguous long gap */
 starter_media_ai_playback_boundary(7,true);
 assert(starter_ai_playout_ready(7,32001,32000,30400));
 assert(starter_ai_playout_status().timeout_releases==1);
''')


def sender_timeline(gaps, count):
    """Media stamps of 20 ms packets; gaps maps packet index to omitted ms."""
    stamps, stamp = [], 20
    for index in range(count):
        stamp += gaps.get(index, 0)
        stamps.append(stamp)
        stamp += 20
    return stamps


# Silence gaps observed in the 2026-11-11 S3 capture (index -> omitted ms).
CAPTURED_GAPS = {4: 20, 5: 120, 19: 120, 20: 320, 21: 40, 25: 40, 26: 260,
                 32: 380, 67: 280, 74: 240, 75: 200, 138: 140, 141: 100,
                 147: 40, 148: 80, 156: 180}


class ESPAIPause(unittest.TestCase):
    def test_captured_silence_gaps_are_restored_as_one_timeline(self):
        stamps = sender_timeline(CAPTURED_GAPS, 184)
        self.assertEqual(stamps[-1], 6240)
        values = ','.join(map(str, stamps))
        omitted = ','.join(str(CAPTURED_GAPS.get(i, 0)) for i in range(184))
        run_playout(f'''
 const uint32_t stamp[]={{{values}}};
 const uint16_t omitted[]={{{omitted}}};
 uint32_t restored=0, played=0;
 starter_ai_playout_reset(7);
 for(unsigned i=0;i<184;i++){{
     unsigned pause=starter_ai_playout_pause_ms(7,stamp[i],3,320);
     assert(pause==omitted[i]);
     restored+=pause; played+=20;
 }}
 /* 3.68 s of voice plus 2.56 s of omitted silence is the sender's 6.24 s. */
 assert(played==3680 && restored==2560 && played+restored==6240);
''')

    def test_pause_is_bounded_and_ignores_ambiguous_stamps(self):
        run_playout(r'''
 starter_ai_playout_reset(7);
 assert(starter_ai_playout_pause_ms(7,1000,3,320)==0);   /* first packet */
 assert(starter_ai_playout_pause_ms(7,1020,3,320)==0);   /* contiguous */
 assert(starter_ai_playout_pause_ms(7,1030,3,320)==0);   /* early stamp */
 assert(starter_ai_playout_pause_ms(7,1060,3,320)==20);  /* one omitted frame */
 assert(starter_ai_playout_pause_ms(7,1080,3,320)==0);
 assert(starter_ai_playout_pause_ms(7,1080,3,320)==0);   /* duplicate */
 assert(starter_ai_playout_pause_ms(7,1060,3,320)==0);   /* late reorder */
 assert(starter_ai_playout_pause_ms(7,1100,3,320)==0);   /* no phantom gap */
 assert(starter_ai_playout_pause_ms(7,6000,3,320)==800); /* long stall capped */
 assert(starter_ai_playout_pause_ms(7,6020,3,960)==0);
 assert(starter_ai_playout_pause_ms(7,6080,3,320)==0);   /* 60 ms packet span */
 assert(starter_ai_playout_pause_ms(7,6120,3,320)==20);
 /* Sender clock reset: restart the timeline instead of inventing silence. */
 assert(starter_ai_playout_pause_ms(7,50,3,320)==0);
 assert(starter_ai_playout_pause_ms(7,70,3,320)==0);
 assert(starter_ai_playout_pause_ms(7,100,3,320)==0);    /* below one frame */
''')

    def test_pause_state_follows_generation_stream_boundary_and_rollover(self):
        run_playout(r'''
 starter_ai_playout_reset(7);
 assert(starter_ai_playout_pause_ms(8,1000,3,320)==0);   /* stale generation */
 assert(starter_ai_playout_pause_ms(0,1000,3,320)==0);
 assert(starter_ai_playout_pause_ms(7,1000,3,0)==0);     /* invalid duration */
 assert(starter_ai_playout_pause_ms(7,1000,3,320)==0);
 assert(starter_ai_playout_pause_ms(7,1200,4,320)==0);   /* other stream restarts */
 assert(starter_ai_playout_pause_ms(7,1220,4,320)==0);
 assert(starter_ai_playout_pause_ms(7,1280,4,320)==40);
 starter_media_ai_playback_boundary(7,true);             /* turn ends */
 assert(starter_ai_playout_pause_ms(7,5000,4,320)==0);
 starter_media_ai_playback_boundary(7,false);            /* turn starts */
 assert(starter_ai_playout_pause_ms(7,9000,4,320)==0);
 assert(starter_ai_playout_pause_ms(7,9040,4,320)==20);
 starter_media_ai_playback_boundary(7,false);            /* duplicate start */
 assert(starter_ai_playout_pause_ms(7,9060,4,320)==0);
 starter_ai_playout_reset(7);
 assert(starter_ai_playout_pause_ms(7,UINT32_MAX-30,3,320)==0);
 assert(starter_ai_playout_pause_ms(7,9,3,320)==20);     /* 32-bit wrap */
 assert(starter_ai_playout_pause_ms(7,30,3,320)==0);
''')


def check_pause_render(source):
    from test_s3_product_regressions import function
    run_media_c(r'''
#include <assert.h>
#include <stdatomic.h>
#include <string.h>
#define STARTER_TIRTC_AI 2
#define ESP_OK 0
#define pdTRUE 1
#define pdMS_TO_TICKS(x) (x)
#define AUDIO_PACKET_MS 20U
#define OPUS_SAMPLE_RATE_HZ 16000U
typedef int SemaphoreHandle_t;
typedef int esp_err_t;
typedef struct {
    void *context;
    esp_err_t (*write_pcm)(void *, const int16_t *, size_t, size_t *);
} audio_adapter_t;
static SemaphoreHandle_t s_audio_output_mutex;
static atomic_bool s_speaker_muted;
static bool s_amp_enabled = true;
static uint32_t generation = 7;
static int takes, gives, take_fail_at, mode_fail_after, mute_after, write_fail_at;
static unsigned writes, tracked;
static size_t last_bytes;
static int16_t s_play_stereo[6000];
static bool same_session(int mode, uint32_t value)
{ return mode == STARTER_TIRTC_AI && value == generation && (!mode_fail_after || writes < (unsigned)mode_fail_after); }
static int xSemaphoreTake(SemaphoreHandle_t m, int ticks)
{ (void)m; (void)ticks; ++takes; return take_fail_at == takes ? 0 : pdTRUE; }
static void xSemaphoreGive(SemaphoreHandle_t m) { (void)m; ++gives; }
static int64_t esp_timer_get_time(void) { return 1000; }
static void audio_playback_track_write(size_t frames) { tracked += (unsigned)frames; }
static void audio_track_dma_write(int64_t start, size_t frames) { (void)start; tracked += (unsigned)frames; }
static esp_err_t fake_write(void *context, const int16_t *pcm, size_t frames, size_t *written)
{
    (void)context;
    ++writes;
    if (write_fail_at == (int)writes) return -1;
    for (size_t i = 0; i < frames * 2U; ++i) assert(pcm[i] == 0);
    *written = frames * 4U; last_bytes = *written;
    if (mute_after == (int)writes) atomic_store(&s_speaker_muted, true);
    return ESP_OK;
}
static audio_adapter_t adapter = {.write_pcm = fake_write};
static audio_adapter_t *s_audio_adapter = &adapter;
''' + function(source, 'play_ai_pause') + r'''
static void reset(void)
{
    memset(s_play_stereo, 0x55, sizeof(s_play_stereo));
    takes = gives = take_fail_at = mode_fail_after = mute_after = write_fail_at = 0;
    writes = tracked = 0; last_bytes = 0; generation = 7;
    atomic_store(&s_speaker_muted, false); s_amp_enabled = true;
}
int main(void)
{
    reset(); assert(play_ai_pause(7, 0) && writes == 0 && takes == 0);
    /* 100 ms is rendered in bounded 20 ms writes, never one long mutex hold. */
    reset(); assert(play_ai_pause(7, 100));
    assert(writes == 5 && tracked == 1600 && last_bytes == 1280);
    assert(takes == 5 && gives == 5);
    reset(); assert(play_ai_pause(7, 30) && writes == 2 && tracked == 480); /* 20 + 10 */
    reset(); mode_fail_after = 2; assert(!play_ai_pause(7, 100));        /* new session */
    assert(writes == 2 && takes == gives);
    reset(); mute_after = 1; assert(!play_ai_pause(7, 100));             /* speaker muted */
    assert(writes == 1 && takes == gives);
    reset(); s_amp_enabled = false; assert(!play_ai_pause(7, 100));
    assert(writes == 0 && takes == gives);
    reset(); write_fail_at = 2; assert(!play_ai_pause(7, 100));
    assert(writes == 2 && tracked == 320 && takes == gives);
    reset(); take_fail_at = 3; assert(!play_ai_pause(7, 100));
    assert(writes == 2 && takes == 3 && gives == 2);
    reset(); generation = 8; assert(!play_ai_pause(7, 100) && writes == 0 && takes == gives);
    return 0;
}
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
 for(unsigned i=0;i<10;i++)starter_ai_playout_admit(7,packet,1);
 assert(audio_rx_prefill(&item) && now==1420); /* 200 ms ready */
 mode=3;item.mode=3;starter_ai_playout_reset(0);
 assert(audio_rx_prefill(&item) && now==1420); /* live media unchanged */
 generation++;assert(!audio_rx_prefill(&item));
}
''')


if __name__ == '__main__':
    unittest.main()
