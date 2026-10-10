"""Successful media callbacks stay quiet at INFO; rejected input remains visible."""
import unittest
from test_s3_product_regressions import ROOT, function, run_c

class ReleaseLogging(unittest.TestCase):
    def test_receive_callback_preserves_delivery_without_periodic_info_io(self):
        source = (ROOT/'platforms/esp-idf/components/starter_tirtc/src/starter_tirtc.c').read_text()
        run_c(r'''
#include <assert.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdatomic.h>
#include <stddef.h>
#define STARTER_TIRTC_AI 2
#define TIRTC_AUDIO_OPUS 4
#define TIRTC_AUDIO_ALAW 1
#define TIRTC_AUDIOSAMPLE_16K16B1C 1
#define TIRTC_AUDIOSAMPLE_8K16B1C 0
static unsigned infos,warnings,delivered;
#define ESP_LOGI(...) ((void)++infos)
#define ESP_LOGW(...) ((void)++warnings)
#define ESP_LOGD(...) ((void)0)
typedef int starter_tirtc_mode_t;
typedef void *tirtc_conn_t;
typedef struct {unsigned stream_id,media,flags,ts,length;} TIRTCFRAMEINFO;
typedef struct {unsigned stream_id,media,flags,timestamp_ms,length;} starter_tirtc_frame_t;
static atomic_int s_mode=2;
static atomic_uint s_active_generation=7,s_downlink_audio_accepted,s_downlink_audio_rejected;
static bool connection_matches(void *c){return c==(void *)1;}
static void receive(int mode,uint32_t generation,const starter_tirtc_frame_t *f,void *data,void *ctx){
    assert(mode==2 && generation==7 && f->length==61 && data);delivered++;
}
static struct {void (*on_audio)(int,uint32_t,const starter_tirtc_frame_t *,void *,void *);void *user_data;} s_handlers={receive,0};
''' + function(source,'on_audio') + r'''
int main(void){TIRTCFRAMEINFO frame={.stream_id=1,.media=4,.flags=1,.length=61};
 for(unsigned i=0;i<1000;i++)on_audio((void *)1,&frame,"payload");
 assert(delivered==1000 && s_downlink_audio_accepted==1000);
 assert(infos==0 && "successful media delivery must not perform INFO serial IO in the SDK callback");
 frame.flags=0;on_audio((void *)1,&frame,"payload");
 assert(warnings==1 && delivered==1000 && s_downlink_audio_rejected==1);
}
''')

    def test_ringtone_handoff_cancellation_stays_quiet(self):
        source=(ROOT/'platforms/esp-idf/components/starter_product/src/starter_product.c').read_text()
        run_c(r'''
#include <assert.h>
#define ESP_OK 0
#define ESP_ERR_INVALID_STATE -1
#define ESP_LOGW(...) ((void)++warnings)
#define pdMS_TO_TICKS(x) (x)
typedef int esp_err_t;
static unsigned warnings,delays;
static void vTaskDelay(unsigned ticks){assert(ticks==100);delays++;}
'''+function(source,'ringtone_report_result')+r'''
int main(void){ringtone_report_result(0);assert(warnings==0&&delays==0);
 ringtone_report_result(-1);assert(warnings==0&&delays==1);
 ringtone_report_result(-2);assert(warnings==1&&delays==2);}
''')

    def test_send_failure_burst_is_counted_with_bounded_logs(self):
        header=ROOT/'platforms/esp-idf/components/starter_media_common/include/starter_send_log.h'
        self.assertTrue(header.exists(), 'per-packet uplink failure logging has no bounded policy')
        run_c(r'''
#include <assert.h>
#include <stdint.h>
#include <stdbool.h>
static unsigned warnings,infos;
#define ESP_LOGW(...) ((void)++warnings)
#define ESP_LOGI(...) ((void)++infos)
'''+'#include "'+str(header)+'"\n'+r'''
int main(void){starter_send_log_t state={0};
 for(unsigned i=0;i<1000;i++)starter_send_log_record(&state,"test",7,3,-1,i*20);
 assert(warnings==4&&infos==0&&state.failures==1000);
 starter_send_log_record(&state,"test",7,3,0,20000);assert(infos==1);
 starter_send_log_record(&state,"test",7,3,0,20020);assert(infos==1);
 starter_send_log_record(&state,"test",8,3,-1,20040);assert(warnings==5&&state.failures==1);
 starter_send_log_record(&state,"test",9,3,-1,UINT32_MAX-10);assert(warnings==6);
 starter_send_log_record(&state,"test",9,3,-1,4990);assert(warnings==7);
}
''')

if __name__=='__main__':unittest.main()
