"""Inject allocation/task failures into the production ESP initialization path."""
import unittest
from test_s3_product_regressions import ROOT, function, run_c

class EspMediaInitFailure(unittest.TestCase):
    def test_failure_releases_application_resources_before_retry(self):
        paths = ['platforms/esp-idf/components/starter_media_s3/starter_media.c',
                 'firmware/esp-idf/esp32p4/waveshare-touch-lcd-43c/components/starter_media/src/starter_media.c']
        for path in paths:
            with self.subTest(path=path):
                source = (ROOT/path).read_text()
                p4 = 'esp32p4' in path
                cleanup = function(source, 'media_init_rollback') if 'static void media_init_rollback(' in source else ''
                run_c(FIXTURE + ('\n#define TEST_P4 1\n' if p4 else '') + cleanup + function(source, 'starter_media_init') + r'''
int main(void) {
 concurrent_commit=true;
 assert(starter_media_init()==ESP_OK && calls==0 && live==0);
 assert(s_ready && !s_initializing);
 s_ready=false;
 for (unsigned failure=1; failure<=100; ++failure) {
  fail_at=failure; calls=live=0;
  int result=starter_media_init();
  if(result==ESP_OK) {
   unsigned before=calls;
   assert(starter_media_init()==ESP_OK && calls==before);
   media_init_rollback();assert(live==0);
   return 0; /* every acquisition boundary was exercised */
  }
  assert(!s_ready && live==0);
  assert(!s_audio_rx_pool && !s_audio_tx_pool && !s_preroll.pcm);
 }
 fail_at=0;calls=0;
 assert(starter_media_init()==ESP_OK && s_ready);
 unsigned before=calls;
 assert(starter_media_init()==ESP_OK && calls==before);
 return 0;
}
''', cflags=['-Wno-unused-variable'])

FIXTURE = r'''
#include <assert.h>
#include <stdatomic.h>
#include <stdint.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
typedef int esp_err_t;
#define ESP_OK 0
#define ESP_FAIL -1
#define ESP_ERR_NO_MEM -2
#define ESP_ERR_INVALID_STATE -3
#define ESP_LOGE(...) ((void)0)
#define ESP_LOGI(...) ((void)0)
#define CONFIG_STARTER_AI_UPLINK_G711A_16K 1
#define XIAOTAI_S3_RAW_RECOGNITION 1
#define MALLOC_CAP_SPIRAM 1
#define MALLOC_CAP_8BIT 2
#define AUDIO_RX_QUEUE_DEPTH 8
#define AUDIO_TX_QUEUE_DEPTH 12
#define JPEG_BUFFER_BYTES 100
#define OPUS_WORKER_TASK_STACK_BYTES 100
#define MEDIA_REALTIME_CORE 1
#define APP_TASK_PRIORITY_AUDIO_PLAYBACK 8
#define APP_TASK_PRIORITY_AUDIO_CAPTURE 9
#define STARTER_AEC_SAMPLE_RATE_HZ 16000
#define STARTER_AEC_CAPTURE_DMA_CHANNELS 2
#define XIAOTAI_CAPTURE_MIC_PLAYBACK_REFERENCE 1
#define pdPASS 1
#define pdTRUE 1
static unsigned fail_at,calls,live;
static void *allocate(void){if(++calls==fail_at)return NULL;live++;return malloc(8);}
static void release(void*p){if(p){assert(live);live--;free(p);}}
#define heap_caps_calloc(...) allocate()
#define heap_caps_malloc(...) allocate()
#define heap_caps_free(p) release(p)
#define esp_ptr_external_ram(p) ((p)!=NULL)
#define xSemaphoreCreateMutex() allocate()
#define vSemaphoreDelete(p) release(p)
#define xQueueCreate(...) allocate()
#define vQueueDelete(p) release(p)
#define xQueueSend(...) (++calls==fail_at?0:pdTRUE)
typedef void *TaskHandle_t;
typedef void (*TaskFunction_t)(void*);
typedef struct {TaskFunction_t function;TaskHandle_t handle;} starter_media_worker_t;
static starter_media_worker_t s_media_workers[4];
static void starter_media_worker_entry(void*p){}
static void starter_media_workers_abort(starter_media_worker_t*w,size_t n){for(size_t i=0;i<n;i++){release(w[i].handle);w[i].handle=NULL;}}
static void starter_media_workers_commit(starter_media_worker_t*w,size_t n){}
static int create_task(TaskHandle_t*out){void*p=allocate();if(!p)return 0;if(out)*out=p;return pdPASS;}
#define xTaskCreateWithCaps(fn,n,s,a,p,h,c) create_task(h)
#define xTaskCreatePinnedToCoreWithCaps(fn,n,s,a,p,h,core,c) create_task(h)
static atomic_bool s_ready,s_initializing;
static bool concurrent_commit;
static bool acquire_init(atomic_bool *p,bool*expected,bool value){
 if(concurrent_commit){s_ready=true;concurrent_commit=false;}
 bool old=*p;if(old!=*expected){*expected=old;return false;}*p=value;return true;
}
#undef atomic_compare_exchange_strong
#define atomic_compare_exchange_strong(p,e,v) acquire_init(p,e,v)
static void *s_audio_output_mutex,*s_preroll_mutex,*s_audio_rx_pool,*s_audio_tx_pool;
static void *s_jpeg_buffer,*s_recognition_raw_pcm,*s_audio_rx_ready_queue,*s_audio_rx_free_queue;
static void *s_audio_tx_ready_queue,*s_audio_tx_free_queue,*s_decode_pcm=(void*)1,*s_play_stereo=(void*)1;
static void *s_g711_encoder,*s_ai_g711_encoder,*s_g711_decoder,*s_opus_encoder,*s_opus_decoder;
static void *s_ns_16k,*s_agc_16k,*s_agc_8k;
static struct {int16_t *pcm;} s_preroll;
static void starter_preroll_init_rate(void*q,void*pcm,size_t n,unsigned r){s_preroll.pcm=pcm;}
typedef struct {int sample_rate_hz,capture_channels,playback_channels,capture_layout;} xiaotai_board_audio_format_t;
static xiaotai_board_audio_format_t s_board_audio_format;
static bool xiaotai_board_audio_format_is_valid(const void*p){return true;}
static int ok(void*p){return 0;}
static bool ready(void*p){return true;}
static xiaotai_board_audio_format_t format(void*p){return (xiaotai_board_audio_format_t){16000,2,2,1};}
static struct {void*context;int(*start)(void*);bool(*ready)(void*);xiaotai_board_audio_format_t(*format)(void*);} s_p4_audio_adapter={0,ok,ready,format},*s_audio_adapter,*s_camera_adapter;
#define szpi_board_audio_adapter() (&s_p4_audio_adapter)
#define szpi_board_camera_adapter() (&s_p4_audio_adapter)
#define szpi_board_init() ESP_OK
#define board_i2c_init() ESP_OK
static int g711_init(void){
 s_g711_encoder=allocate();if(!s_g711_encoder)return ESP_FAIL;
 s_ai_g711_encoder=allocate();if(!s_ai_g711_encoder)return ESP_FAIL;
 s_g711_decoder=allocate();return s_g711_decoder?ESP_OK:ESP_FAIL;
}
static int opus_init(void){
 s_opus_encoder=allocate();if(!s_opus_encoder)return ESP_FAIL;
 s_opus_decoder=allocate();return s_opus_decoder?ESP_OK:ESP_FAIL;
}
static int opus_and_3a_init(void){
 if(opus_init()!=0)return ESP_FAIL;
 s_ns_16k=allocate();s_agc_16k=allocate();s_agc_8k=allocate();
 return s_ns_16k && s_agc_16k && s_agc_8k?ESP_OK:ESP_FAIL;
}
#define camera_init() ESP_OK
#define starter_aec_init() ESP_OK
#define starter_aec_frame_samples() 320U
#define starter_aec_deinit() ((void)0)
#define esp_g711_enc_close(p) release(p)
#define esp_g711_dec_close(p) release(p)
#define esp_opus_enc_close(p) release(p)
#define esp_opus_dec_close(p) release(p)
#define ns_destroy(p) release(p)
#define esp_agc_close(p) release(p)
static void audio_sink_task(void*p){}
static void audio_uplink_task(void*p){}
static void audio_capture_task(void*p){}
static void camera_task(void*p){}
'''
if __name__ == '__main__': unittest.main()
