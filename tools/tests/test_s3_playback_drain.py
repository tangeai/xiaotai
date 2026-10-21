"""S3 public drain status must include queued and worker-held final speech."""
import re
import unittest
from test_s3_product_regressions import MEDIA, ROOT, function, run_c

def playback_fixture(source):
    board = (ROOT / 'boards/lckfb/esp32s3/board_config.h').read_text()
    constants = '\n'.join(line for line in board.splitlines()
        if line.startswith(('#define AUDIO_I2S_DMA_', '#define AUDIO_HW_SAMPLE_RATE_HZ')))
    state = r'''
#define portENTER_CRITICAL(p) ((void)(p))
static void (*playback_unlock_hook)(void);
#define portEXIT_CRITICAL(p) do { (void)(p); if(playback_unlock_hook)playback_unlock_hook(); } while(0)
static int s_playback_timing_lock;
static uint64_t s_audio_pcm_frames;
static int64_t s_audio_dma_until_us;
'''
    helpers = ''.join(function(source, name) for name in (
        'audio_playback_timing_reset', 'audio_playback_track_write', 'audio_playback_timing'))
    return '\n'+constants+'\n'+state+helpers+'\n'

def runtime_drain_fixture():
    runtime = (ROOT / 'platforms/esp-idf/components/starter_runtime/src/starter_runtime.c').read_text()
    protocol = (ROOT / 'product/src/xiaotai_ai_protocol.c').read_text()
    header = ROOT / 'product/include/xiaotai_ai_protocol.h'
    return '\n#include "'+str(header)+'"\n'+r'''
#define STARTER_RUNTIME_AI_ACTIVE 3
#define TAG "test"
#define ESP_LOGI(...) fake_log(__VA_ARGS__)
static void fake_log(const char *tag,const char *format,...){}
static xiaotai_ai_end_drain_t s_ai_end_drain;
static bool s_ai_transport_closed,check_runtime;
static unsigned finished;
static uint32_t session_generation(void){return 7;}
static int session_state(void){return 3;}
static int64_t now_ms(void){return clock_ms;}
static void starter_media_set_uplink_enabled(bool enabled){assert(!enabled);}
static void log_ai_playback_status(const char *stage){}
static void finish_session(int error){assert(error==0);finished++;}
'''+''.join(function(protocol, name) for name in (
        'xiaotai_ai_end_drain_begin','xiaotai_ai_end_drain_cancel','xiaotai_ai_end_drain_step'))+''.join(
        function(runtime, name) for name in ('begin_ai_end_drain','service_ai_end_drain'))+'\n'

class S3PlaybackDrain(unittest.TestCase):
    def test_start_publishes_active_only_after_output_ready(self):
        body = function(MEDIA.read_text(), 'video_enabled_for_mode') + function(MEDIA.read_text(), 'starter_media_start')
        atomics = sorted(set(re.findall(r'atomic_\w+\(\s*&([a-zA-Z_][a-zA-Z_0-9]*)', body)) | {'s_audio_playback_pending'})
        run_c(r'''
#include <assert.h>
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdatomic.h>
#define ESP_OK 0
#define ESP_ERR_INVALID_STATE -1
#define ESP_ERR_TIMEOUT -2
#define ESP_LOGI(...) ((void)0)
#define ESP_LOGE(...) ((void)0)
#define pdTRUE 1
#define pdMS_TO_TICKS(x) (x)
#define portMAX_DELAY 0
#define STARTER_TIRTC_H5 1
#define STARTER_TIRTC_AI 2
#define STARTER_TIRTC_VOIP 3
#define STARTER_TIRTC_CALL 4
#define STARTER_TIRTC_ROOM 5
typedef int esp_err_t;
typedef int starter_tirtc_mode_t;
''' + 'static atomic_uint '+','.join(atomics)+';\n' + r'''
static int s_preroll_mutex=1,s_audio_output_mutex=2,s_preroll;
static unsigned s_expected_wake_token,canceled,timing_resets;
static bool amp,timeout,locked;
static int64_t esp_timer_get_time(void){return 1;}
static bool starter_preroll_bind(int*a,unsigned b,unsigned c,int64_t d){return true;}
static void starter_preroll_clear(int*a){}
static void starter_media_cancel_pcm8k_playback(void){canceled++;}
static void drain_audio_rx_ready_queue(void){}
static int xSemaphoreTake(int mutex,int delay){
 if(mutex==2){assert(!s_active && canceled);if(timeout)return 0;locked=true;}return 1;
}
static void xSemaphoreGive(int mutex){if(mutex==2){assert(locked);locked=false;}}
static int audio_output_set_locked(bool enabled){assert(locked&&!s_active);amp=enabled;return 0;}
static void audio_playback_timing_reset(bool reset_total){assert(locked && !s_active && reset_total);timing_resets++;}
''' + body + r'''
int main(void){s_ready=true;s_audio_playback_pending=1;
 assert(starter_media_start(3,7)==0&&s_active&&amp&&timing_resets==1);
 assert(s_audio_playback_pending==1); /* old sink still owns a slot */
 s_active=false;amp=false;timeout=true;
 assert(starter_media_start(3,8)==-2&&!s_active&&s_generation==0&&timing_resets==1);
}
''')


    def test_final_burst_remains_pending_until_sink_finishes(self):
        source = MEDIA.read_text()
        constants = '\n'.join(line for line in source.splitlines()
            if line.startswith(('#define AUDIO_RX_QUEUE_', '#define AUDIO_RX_LIVE_', '#define AUDIO_RX_PREFILL_')))
        header = (ROOT / 'platforms/esp-idf/components/starter_media_common/include/starter_media.h').read_text()
        status_type = header[header.index('typedef struct {'):header.index('} starter_media_status_t;')+len('} starter_media_status_t;')]
        status = function(source, 'starter_media_status')
        existing = {'s_audio_received','s_audio_dropped','s_audio_rx_overflow','s_audio_rx_prefill_waits','s_audio_rx_burst_window','s_audio_rx_burst_max','s_audio_rx_queue_peak'}
        atomics = sorted(set(re.findall(r'atomic_\w+\(\s*&([a-zA-Z_][a-zA-Z_0-9]*)', status)) - existing)
        atomics = sorted(set(atomics) | {'s_audio_playback_pending','s_audio_playback_active','s_audio_rx_last_ms'})
        run_c(r'''
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include <stdatomic.h>
#include <setjmp.h>
#define pdTRUE 1
#define pdFALSE 0
#define portMAX_DELAY 999999U
#define pdMS_TO_TICKS(x) (x)
#define ESP_LOGE(...) ((void)0)
#define AUDIO_RX_BYTES 1500U
#define AUDIO_PACKET_MS 20U
#define AUDIO_PLAYBACK_IDLE_MS 60U
#define STARTER_TIRTC_AI 2
''' + constants + r'''
typedef unsigned TickType_t;
typedef int starter_tirtc_mode_t;
typedef struct {size_t length;uint32_t timestamp_ms;} starter_tirtc_frame_t;
typedef struct {int mode;uint32_t generation;starter_tirtc_frame_t frame;
    uint8_t payload[AUDIO_RX_BYTES];} audio_rx_item_t;
typedef struct {uint8_t slots[64];unsigned head,count;} queue_t;
static queue_t ready,free_slots;
static queue_t *s_audio_rx_ready_queue=&ready,*s_audio_rx_free_queue=&free_slots;
static audio_rx_item_t pool[AUDIO_RX_QUEUE_DEPTH],*s_audio_rx_pool=pool;
static atomic_uint s_audio_dropped,s_audio_received;
static atomic_uint s_audio_rx_overflow,s_audio_rx_prefill_waits;
static atomic_uint_fast32_t s_audio_rx_burst_window,s_audio_rx_burst_max,
    s_audio_rx_queue_peak;
static unsigned clock_ms=1000,played,expected_packets=19;
static bool fail_publish,discard_while_held;
static void drain_audio_rx_ready_queue(void);

static jmp_buf done;
static bool same_session(int mode,uint32_t generation){return mode==2 && generation==7;}
static int64_t esp_timer_get_time(void){return (int64_t)clock_ms*1000;}
static unsigned uxQueueMessagesWaiting(queue_t *q){return q->count;}
static int xQueueSend(queue_t *q,const uint8_t *v,unsigned timeout){
    if(q==&ready && fail_publish){fail_publish=false;return pdFALSE;}
    if(q->count==AUDIO_RX_QUEUE_DEPTH)return pdFALSE;
    q->slots[(q->head+q->count)%64]=*v;q->count++;return pdTRUE;
}


static void vTaskDelay(unsigned ms){clock_ms+=ms;}
static int xQueueReceive(queue_t *q,uint8_t *v,unsigned timeout){
    if(!q->count){if(timeout==portMAX_DELAY)longjmp(done,1);return 0;}
    *v=q->slots[q->head];q->head=(q->head+1)%64;q->count--;return 1;
}
''' + status_type + '\natomic_uint '+','.join(atomics)+';\n' + r'''
static bool uplink_allowed(int mode){return true;}
''' + function(source, 'update_max_counter') + function(source, 'audio_rx_record_arrival') + function(source, 'starter_media_submit_audio') + playback_fixture(source) + status + runtime_drain_fixture() + r'''
static bool play_audio_item(const audio_rx_item_t *item){
    if(check_runtime){service_ai_end_drain();assert(finished==0);}
    starter_media_status_t status=starter_media_status();
    assert(status.audio_playback_active);
    if(discard_while_held){
        drain_audio_rx_ready_queue();
        assert(starter_media_status().audio_playback_pending==1);
    }else assert(status.audio_playback_pending==expected_packets-played);
    assert(item->frame.timestamp_ms==played*20);
    played++;clock_ms+=20;audio_playback_track_write(320);return true;
}
''' + function(source, 'audio_rx_prefill') + function(source, 'audio_sink_task') + function(source, 'drain_audio_rx_ready_queue') + r'''
static void finish_last_write_during_status(void){
    playback_unlock_hook=NULL;
    audio_playback_track_write(320);
    s_audio_playback_active=false;s_audio_playback_pending=0;
}
int main(void){
    s_audio_playback_pending=1;s_audio_playback_active=true;
    playback_unlock_hook=finish_last_write_during_status;
    starter_media_status_t racing=starter_media_status();
    assert(racing.audio_playback_pending || racing.audio_playback_active);
    audio_playback_timing_reset(true);
    for(unsigned i=0;i<AUDIO_RX_QUEUE_DEPTH;i++){uint8_t slot=i;xQueueSend(&free_slots,&slot,0);}
    uint8_t payload[31]={0};
    for(unsigned i=0;i<19;i++){
        starter_tirtc_frame_t frame={.length=31,.timestamp_ms=i*20};
        starter_media_submit_audio(2,7,&frame,payload);
    }
    starter_media_status_t status=starter_media_status();
    assert(status.audio_received==19);
    assert(status.audio_playback_pending==19 && "end_session must see unplayed final packets");
    assert(status.audio_rx_last_ms==1000);
    if(!setjmp(done))audio_sink_task(NULL);
    status=starter_media_status();
    assert(played==19 && status.audio_playback_pending==0 && status.audio_playback_active);
    assert(status.audio_playback_pcm_ms==380 && status.audio_playback_dma_ms==90);
    clock_ms+=89;assert(starter_media_status().audio_playback_active);
    clock_ms++;assert(!starter_media_status().audio_playback_active);
    audio_playback_timing_reset(false);
    assert(starter_media_status().audio_playback_pcm_ms==380);
    audio_playback_timing_reset(true);
    assert(starter_media_status().audio_playback_pcm_ms==0);
    /* User trace: direct transport close, 249 received / 242 written.
     * Zero arrival grace must still retain all seven packets and DMA tail. */
    clock_ms=875639;played=0;expected_packets=7;check_runtime=true;
    for(unsigned i=0;i<7;i++){
        starter_tirtc_frame_t packet={.length=31,.timestamp_ms=i*20};
        starter_media_submit_audio(2,7,&packet,payload);
    }
    begin_ai_end_drain(0,3000,true);
    service_ai_end_drain();assert(finished==0 && s_ai_transport_closed);
    if(!setjmp(done))audio_sink_task(NULL);
    assert(played==7);
    service_ai_end_drain();assert(finished==0);
    clock_ms+=89;service_ai_end_drain();assert(finished==0);
    clock_ms++;service_ai_end_drain();assert(finished==1);
    check_runtime=false;
    starter_tirtc_frame_t frame={.length=31};
    for(unsigned i=0;i<80;i++)starter_media_submit_audio(2,7,&frame,payload);
    assert(starter_media_status().audio_playback_pending==64);
    drain_audio_rx_ready_queue();
    assert(starter_media_status().audio_playback_pending==0 && free_slots.count==64);
    fail_publish=true;starter_media_submit_audio(2,7,&frame,payload);
    assert(starter_media_status().audio_playback_pending==0 && free_slots.count==64);
    starter_media_submit_audio(2,6,&frame,payload); /* stale generation */
    assert(starter_media_status().audio_playback_pending==0);
    played=0;discard_while_held=true;
    for(unsigned i=0;i<3;i++)starter_media_submit_audio(2,7,&frame,payload);
    if(!setjmp(done))audio_sink_task(NULL);
    assert(played==1 && starter_media_status().audio_playback_pending==0 && free_slots.count==64);
}
''')

if __name__ == '__main__':
    unittest.main()
