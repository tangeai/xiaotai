"""Replay P4/BK AI uplink workers through DSP, codec and SDK boundaries."""
import unittest
from test_s3_product_regressions import ROOT, function, run_c
import test_s3_opus_media as opus
COMMON = opus.COMMON

P4=ROOT/'firmware/esp-idf/esp32p4/waveshare-touch-lcd-43c/components/starter_media/src/starter_media.c'
BK=ROOT/'platforms/beken/runtime/src'
RESAMPLER=ROOT/'platforms/common'
FIR=(RESAMPLER/'src/starter_audio_resampler.c').read_text()

class AiAlawRollout(unittest.TestCase):
    def test_p4_preroll_tail_and_live_join(self):
        opus.S3OpusMedia().check_preroll(g711=False, source_path=P4)

    def test_p4_keeps_16k_dsp_before_8k_wire_encoding(self):
        source=P4.read_text()
        run_c('#define CONFIG_STARTER_AI_UPLINK_G711A 1\n'+COMMON+FIR+r'''
#define AUDIO_PROCESS_16K_SAMPLES 160
#define AUDIO_PROCESS_8K_SAMPLES 80
#define OPUS_SAMPLE_RATE_HZ 16000
#define AUDIO_TRANSPORT_SAMPLE_RATE_HZ 8000
#define ESP_AGC_SUCCESS 0
#undef ESP_LOGW
#define ESP_LOGW(...) ((void)++encode_warnings)
static unsigned encode_warnings;
static void *s_ns_16k,*s_agc_16k=(void *)1,*s_agc_8k=(void *)2;
static unsigned ns_calls,agc_calls,alaw_calls,sends,reads,returned;
static bool bad_count,stale,muted,fail_codec,late_mute,late_session;
static int16_t expected_pcm;
static atomic_uint s_audio_sent,s_mute_epoch,s_agc_failures;
static atomic_bool s_microphone_muted;
static int s_audio_tx_ready_queue=1,s_audio_tx_free_queue=2;
typedef struct {int mode;uint32_t generation,timestamp_ms;unsigned mute_epoch;size_t sample_count;int16_t pcm[320];} audio_tx_item_t;
static audio_tx_item_t pool[1],*s_audio_tx_pool=pool;
static jmp_buf done;
static int64_t esp_timer_get_time(void){return 1000000;}
static bool starter_tirtc_audio_ready(void){return true;}
static bool uplink_allowed(int m){return true;}
static int xQueueReceive(int q,uint8_t *slot,int wait){
 if(reads++==4)longjmp(done,1);
 mode=reads==3 ? STARTER_TIRTC_H5 : STARTER_TIRTC_AI;
 active_generation=reads>2 ? 8:7;
 pool[0]=(audio_tx_item_t){.mode=mode,.generation=stale?1:active_generation,.timestamp_ms=20*reads,
 .sample_count=bad_count?159:(mode==STARTER_TIRTC_AI?320:160)};
 s_microphone_muted=muted;
 for(unsigned i=0;i<pool[0].sample_count;i++)pool[0].pcm[i]=100;
 *slot=0;return 1;
}
static int xQueueSend(int q,uint8_t *slot,int wait){returned++;return 1;}
static void ns_process(void *h,int16_t *in,int16_t *out){
 assert(mode==STARTER_TIRTC_AI);ns_calls++;
 for(int i=0;i<160;i++){assert(in[i]==100);out[i]=200;}
}
static int esp_agc_process(void *h,int16_t *in,int16_t *out,size_t count,unsigned rate){
 bool ai=mode==STARTER_TIRTC_AI;
 assert(h==(ai?s_agc_16k:s_agc_8k) && count==(ai?160:80) && rate==(ai?16000:8000));
 agc_calls++;expected_pcm=ai?400:200;
 for(unsigned i=0;i<count;i++){assert(in[i]==(ai?200:100));out[i]=expected_pcm;}
 return 0;
}
static int esp_opus_enc_process(void *h,esp_audio_enc_in_frame_t *in,esp_audio_enc_out_frame_t *out){assert(!"AI must not encode Opus");return -1;}
static int esp_g711_enc_process(void *h,esp_audio_enc_in_frame_t *in,esp_audio_enc_out_frame_t *out){
 assert(in->len==320);alaw_calls++;
 for(unsigned i=0;i<160;i++)assert(((int16_t *)in->buffer)[i]>=expected_pcm-1 && ((int16_t *)in->buffer)[i]<=expected_pcm+1);
 if(late_mute){s_microphone_muted=true;atomic_fetch_add(&s_mute_epoch,1);}
 if(late_session)active_generation++;
 out->encoded_bytes=160;return fail_codec?-1:0;
}
static int starter_tirtc_send_opus(uint32_t ts,const void *d,uint32_t n){assert(0);return -1;}
static int starter_tirtc_send_alaw(uint32_t ts,const void *d,uint32_t n){assert(n==160 && ts==reads*20);sends++;return 0;}
''' + function(source,'audio_uplink_task')+r'''
static void replay(void){reads=0;if(!setjmp(done))audio_uplink_task(NULL);}
int main(void){
 replay();assert(sends==4 && ns_calls==6 && agc_calls==8 && alaw_calls==4 && returned==4);
 muted=true;replay();muted=false;stale=true;replay();stale=false;
 bad_count=true;replay();bad_count=false;
 assert(sends==4 && alaw_calls==4 && returned==16);
 fail_codec=true;replay();assert(sends==4 && returned==20 && encode_warnings==4);fail_codec=false;
 late_session=true;replay();late_session=false;assert(sends==4 && returned==24 && encode_warnings==4);
 late_mute=true;replay();assert(sends==4 && returned==28 && encode_warnings==4);
}
''',include_dirs=[RESAMPLER/'include'])

    def test_bk_capture_preserves_aec_ns_agc_and_downsamples(self):
        source=(BK/'xiaotai_audio.c').read_text()
        run_c(FIR+r'''
#include <assert.h>
#include <stdatomic.h>
#define BK_OK 0
#define XIAOTAI_AUDIO_CODEC_ALAW_8K 0
#define XIAOTAI_AUDIO_CODEC_OPUS_16K 1
#define AUDIO_MAX_FRAME_SAMPLES 320
#define AUDIO_ALAW_FRAME_BYTES 160
#define OPUS_MAX_PACKET_BYTES 512
#define OPUS_FRAME_SAMPLES 320
#define UPLINK_LEVEL_LOG_INTERVAL_FRAMES 50
#define BK_LOGI(...) ((void)0)
#define BK_LOGD(...) ((void)0)
#define BK_LOGW(...) ((void)0)
typedef void *beken_thread_arg_t;
typedef struct {unsigned input_peak,output_peak,limited_samples;} xiaotai_audio_agc_stats_t;
typedef int (*xiaotai_audio_uplink_fn)(const uint8_t*,size_t,uint32_t,void*);
static int s_codec=1,s_uplink_codec=0,s_uplink_ns,s_uplink_agc,s_capture_stopped;
static void *s_opus_encoder,*s_capture_thread,*s_uplink_context;
static size_t s_frame_samples=320;
static bool s_capture_run=true;
static unsigned s_uplink_ns_speech_frames,s_uplink_ns_noise_frames,s_uplink_ns_failures,s_uplink_agc_failures,
 s_uplink_level_frames,s_uplink_frames,s_uplink_rejected,stage,frames,encoded_samples;
static atomic_bool s_microphone_muted,s_uplink_enabled=true;
static starter_audio_resampler_16k_to_8k_t s_uplink_resampler;
static int read_pcm(void *c,int16_t *pcm,size_t count,size_t *bytes){
 if(frames==3)s_capture_run=false;
 assert(count==s_frame_samples);frames++;stage=0;
 for(unsigned i=0;i<count;i++)pcm[i]=100;
 *bytes=count*2;return 0;
}
static struct {void *context;int (*read_pcm)(void *,int16_t *,size_t,size_t *);} adapter={0,read_pcm},*s_board_audio=&adapter;
static void aec_process_capture(int16_t *p,size_t n){assert(stage++==0 && n==s_frame_samples);for(unsigned i=0;i<n;i++)p[i]=200;}
static bool xiaotai_audio_ns_process(void *h,int16_t *p,size_t n,bool *speech){assert(stage++==1 && n==320 && p[0]==200);for(unsigned i=0;i<n;i++)p[i]=300;*speech=true;return true;}
static bool xiaotai_audio_agc_process(void *h,int16_t *p,size_t n,void *s){assert(stage++==(s_codec==1?2:1) && n==s_frame_samples && p[0]==(s_codec==1?300:200));for(unsigned i=0;i<n;i++)p[i]=400;return true;}
static int opus_encode(void *h,int16_t *p,unsigned n,uint8_t *o,size_t cap){assert(!"AI must not encode Opus");return -1;}
static uint8_t linear2alaw(int sample){assert(stage==(s_codec==1?3:2) && sample>=399 && sample<=401);encoded_samples++;return 0x42;}
static void rtos_delay_milliseconds(int ms){}
static unsigned rtos_get_time(void){return 1000+frames*20;}
static void rtos_set_semaphore(void *p){}
static void rtos_delete_thread(void *p){}
static int send(const uint8_t *data,size_t size,uint32_t ts,void *c){assert(size==160 && ts==1000+frames*20);for(unsigned i=0;i<size;i++)assert(data[i]==(s_microphone_muted?0xd5:0x42));return 0;}
static xiaotai_audio_uplink_fn s_uplink=send;
''' + function(source,'capture_task')+r'''
static void replay(void){frames=0;s_capture_run=true;starter_audio_resampler_16k_to_8k_reset(&s_uplink_resampler);capture_task(NULL);}
int main(void){
 replay();assert(s_uplink_frames==4 && s_uplink_rejected==0 && encoded_samples==640);
 s_microphone_muted=true;replay();assert(s_uplink_frames==8 && encoded_samples==640);
 s_microphone_muted=false;s_codec=0;s_frame_samples=160;
 replay();assert(s_uplink_frames==12 && encoded_samples==1280);
 s_uplink_enabled=false;replay();assert(s_uplink_frames==12 && encoded_samples==1920);
 s_uplink_enabled=true;s_codec=1;s_frame_samples=320;
 replay();assert(s_uplink_frames==16 && encoded_samples==2560);
}
''',include_dirs=[RESAMPLER/'include'])

    def test_bk_ai_prepare_and_wire_contract(self):
        source=(BK/'xiaotai_tirtc.c').read_text()
        run_c(r'''
#include <assert.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdatomic.h>
#define BK_OK 0
#define TIRTC_E_BUSY -1
#define TIRTC_E_INVALID_HANDLE -2
#define CONNECTION_STREAM 1
#define CONNECTION_AI 2
#define CONNECTION_VOIP 3
#define CONNECTION_ROOM 4
#define AI_AUDIO_ID 1
#define ROOM_AUDIO_ID 1
#define VOIP_AUDIO_ID 0
#define STREAM_UP_AUDIO_ID 10
#define TIRTC_AUDIO_OPUS 9
#define TIRTC_AUDIO_ALAW 8
#define TIRTC_AUDIOSAMPLE_8K16B1C 80
#define TIRTC_AUDIOSAMPLE_16K16B1C 160
#define XIAOTAI_AUDIO_CODEC_ALAW_8K 0
#define XIAOTAI_AUDIO_CODEC_OPUS_16K 1
typedef void *tirtc_conn_t;
typedef int connection_mode_t;
typedef struct {unsigned stream_id,media,flags,ts,length;} TIRTCFRAMEINFO;
static atomic_uintptr_t s_connection=1;
static atomic_int s_connection_mode=CONNECTION_AI;
static atomic_bool s_active_disconnect_queued,s_audio_subscribed=true;
static bool enabled=true;
static unsigned sends,starts,subscribes,stops;
static int start_error,subscribe_error;
static int TiRtcSendAudioStream(tirtc_conn_t c,const TIRTCFRAMEINFO *f,const void *data){
 assert(c==(void *)1 && data && f->length==160 && f->ts==20);
 assert(f->media==TIRTC_AUDIO_ALAW && f->flags==TIRTC_AUDIOSAMPLE_8K16B1C);
 unsigned id=s_connection_mode==CONNECTION_AI || s_connection_mode==CONNECTION_ROOM ? 1 : (s_connection_mode==CONNECTION_VOIP?0:10);
 assert(f->stream_id==id);sends++;return 0;
}
static void xiaotai_audio_set_uplink_enabled(bool value){enabled=value;}
static int xiaotai_audio_start_duplex(int up,int down,void *fn,void *ctx){
 assert(up==0 && down==1 && fn && !enabled && !s_audio_subscribed);starts++;return start_error;
}
static int xiaotai_audio_start(int codec,void *fn,void *ctx){assert(!"AI must configure independent directions");return -1;}
static int TiRtcSubscribeAudio(tirtc_conn_t c,unsigned id){assert(c==(void*)1 && id==1);subscribes++;return subscribe_error;}
static int xiaotai_audio_stop(void){stops++;return 0;}
static int normalize_tirtc_result(int rc){return rc;}
'''+function(source,'send_encoded_audio')+function(source,'xiaotai_tirtc_ai_prepare_media')+r'''
int main(void){
 char data[160]={0};
 assert(send_encoded_audio((uint8_t *)data,160,20,NULL)==0);
 s_audio_subscribed=false;assert(send_encoded_audio((uint8_t *)data,160,20,NULL)==-1);
 s_audio_subscribed=true;s_active_disconnect_queued=true;assert(send_encoded_audio((uint8_t *)data,160,20,NULL)==-1);
 s_active_disconnect_queued=false;
 for(int mode=1;mode<=4;mode++){s_connection_mode=mode;assert(send_encoded_audio((uint8_t *)data,160,20,NULL)==0);}
 s_connection_mode=CONNECTION_AI;
 assert(xiaotai_tirtc_ai_prepare_media()==0 && starts==1 && subscribes==1 && !enabled);
 start_error=-5;assert(xiaotai_tirtc_ai_prepare_media()==-5 && subscribes==1 && stops==1);
 start_error=0;subscribe_error=-6;assert(xiaotai_tirtc_ai_prepare_media()==-6 && stops==2);
 s_connection=0;assert(xiaotai_tirtc_ai_prepare_media()==-2 && starts==3);
}
''')

if __name__=='__main__':unittest.main()
