"""Exercise S3 media workers at the codec, TiRTC and board PCM boundaries."""
import unittest
from test_s3_product_regressions import MEDIA, ROOT, function, run_c

COMMON = r'''
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include <stdatomic.h>
#include <setjmp.h>
#define ESP_OK 0
#define ESP_AUDIO_ERR_OK 0
#define ESP_LOGI(...) ((void)0)
#define ESP_LOGW(...) ((void)0)
#define pdTRUE 1
#define portMAX_DELAY 0
#define pdMS_TO_TICKS(x) (x)
#define STARTER_TIRTC_AI 2
#define STARTER_TIRTC_H5 1
#define STARTER_TIRTC_ROOM 5
#define AUDIO_PACKET_SAMPLES 160U
#define ALAW_PACKET_SAMPLES 160U
#define OPUS_PACKET_SAMPLES 320U
#define AUDIO_PACKET_SAMPLES_MAX 320U
#define OPUS_MAX_PACKET_BYTES 512U
#define AUDIO_RX_BYTES 1500U
#define AUDIO_PLAYBACK_I2S_VALUES_PER_INPUT 4U
typedef int starter_tirtc_mode_t;
typedef int esp_err_t;
typedef int esp_audio_err_t;
typedef struct {uint8_t *buffer;size_t len;} esp_audio_enc_in_frame_t;
typedef struct {uint8_t *buffer;size_t len,encoded_bytes;} esp_audio_enc_out_frame_t;
typedef struct {uint8_t *buffer;size_t len;} esp_audio_dec_in_raw_t;
typedef struct {uint8_t *buffer;size_t len,decoded_size;} esp_audio_dec_out_frame_t;
typedef struct {int sample_rate,channel,bits_per_sample;} esp_audio_dec_info_t;
void *s_g711_encoder,*s_opus_encoder,*s_g711_decoder,*s_opus_decoder;
typedef struct {uint32_t generation,count;uint64_t total_us;} audio_timing_average_t;
audio_timing_average_t s_encode_average,s_decode_average,s_lock_average,s_write_average;
atomic_uint_fast32_t s_audio_encode_avg_us,s_audio_decode_avg_us,
    s_audio_lock_avg_us,s_audio_write_avg_us,s_audio_encode_max_us;
static int mode=STARTER_TIRTC_AI;
int codec_error;
static uint32_t active_generation=7;
static unsigned resets;
static int esp_opus_enc_reset(void *handle){resets++;return 0;}
static int esp_opus_dec_reset(void *handle){resets++;return 0;}
static bool same_session(int m,uint32_t g){return m==mode && g==active_generation;}
'''


class S3OpusMedia(unittest.TestCase):
    def test_codec_configuration_and_server_negotiation_agree(self):
        runtime=(ROOT / 'platforms/esp-idf/components/starter_runtime/src/starter_runtime.c').read_text()
        run_c(COMMON + r'''
#define ESP_FAIL -1
#define ESP_AUDIO_SAMPLE_RATE_16K 16000
#define ESP_AUDIO_MONO 1
#define ESP_AUDIO_BIT16 16
#define ESP_OPUS_ENC_FRAME_DURATION_20_MS 20
#define ESP_OPUS_DEC_FRAME_DURATION_20_MS 20
#define ESP_OPUS_ENC_APPLICATION_VOIP 1
typedef struct {int sample_rate,channel,bits_per_sample,bitrate,frame_duration,
    application_mode,complexity;bool enable_vbr;} esp_opus_enc_config_t;
typedef struct {int sample_rate,channel,frame_duration;} esp_opus_dec_cfg_t;
#define ESP_OPUS_ENC_CONFIG_DEFAULT() {0}
#define ESP_OPUS_DEC_CONFIG_DEFAULT() {0}
static bool decoder_fails;
static unsigned closes;
static int esp_opus_enc_open(void *cfg,unsigned size,void **handle){
    esp_opus_enc_config_t *c=cfg;assert(size==sizeof(*c));
    assert(c->sample_rate==16000 && c->channel==1 && c->bits_per_sample==16);
    assert(c->bitrate==16000 && c->frame_duration==20 && c->enable_vbr && c->complexity==0);
    *handle=(void *)1;return 0;
}
static int esp_opus_dec_open(void *cfg,unsigned size,void **handle){
    esp_opus_dec_cfg_t *c=cfg;assert(size==sizeof(*c));
    assert(c->sample_rate==16000 && c->channel==1 && c->frame_duration==20);
    *handle=decoder_fails ? NULL : (void *)2;return decoder_fails ? -1 : 0;
}
static void esp_opus_enc_close(void *handle){assert(handle==(void *)1);closes++;}
typedef struct {int type,valueint;const char *valuestring;} cJSON;
static cJSON codec={2,0,"opus"},rate={3,16000,NULL},channels={3,1,NULL};
static bool cJSON_IsObject(const cJSON *p){return p && p->type==1;}
static bool cJSON_IsString(const cJSON *p){return p && p->type==2;}
static bool cJSON_IsNumber(const cJSON *p){return p && p->type==3;}
static const cJSON *cJSON_GetObjectItemCaseSensitive(const cJSON *p,const char *key){
    if(strcmp(key,"codec")==0)return &codec;
    if(strcmp(key,"sample_rate")==0)return &rate;
    return &channels;
}
''' + function(MEDIA.read_text(),'opus_init') + function(runtime,'ai_audio_profile_valid') + r'''
int main(void){
    assert(opus_init()==0 && s_opus_encoder && s_opus_decoder);
    decoder_fails=true;assert(opus_init()==-1 && !s_opus_encoder && closes==1);
    cJSON profile={1,0,NULL};assert(ai_audio_profile_valid(&profile));
    codec.valuestring="alaw";assert(!ai_audio_profile_valid(&profile));
    codec.valuestring="opus";rate.valueint=8000;assert(!ai_audio_profile_valid(&profile));
    rate.valueint=16000;channels.valueint=2;assert(!ai_audio_profile_valid(&profile));
    channels.valueint=1;assert(!ai_audio_profile_valid(NULL));
}
''')

    def test_wake_tail_and_live_audio_join_without_rate_or_timestamp_mixing(self):
        source = MEDIA.read_text()
        capture = function(source, 'audio_capture_task')
        start = capture.index('        bool replay = false;')
        body = capture[start:capture.rfind('\n    }')]
        preroll = ROOT / 'platforms/esp-idf/components/starter_media_common'
        run_c(COMMON + '#include "' + str(preroll / 'include/starter_preroll.h') + '"\n' +
              (preroll / 'src/starter_preroll.c').read_text().replace('#include "starter_preroll.h"', '') + r'''
#define AUDIO_PACKET_MS 20U
static starter_preroll_t s_preroll;
static uint32_t s_expected_wake_token;
static int s_preroll_mutex;
static size_t packet_samples;
static uint32_t next_timestamp_ms;
static int16_t packet_pcm[320],preroll_8k[160],sent[2][320];
static uint32_t timestamps[2];
static unsigned packets;
static struct {const int16_t *pcm_16k,*pcm_8k;size_t samples_16k,samples;} clean;
static int xSemaphoreTake(int h,int timeout){return pdTRUE;}
static void xSemaphoreGive(int h){}
static bool starter_tirtc_audio_ready(void){return true;}
static int64_t esp_timer_get_time(void){return 1000000;}
static bool enqueue_uplink_pcm(int m,uint32_t g,uint32_t ts,const int16_t *pcm,size_t samples,unsigned epoch){
    assert(m==STARTER_TIRTC_AI && g==7 && samples==320 && packets<2);
    timestamps[packets]=ts;memcpy(sent[packets++],pcm,samples*2);return true;
}
''' + function(source, 'upsample_ai_preroll') + r'''
static void capture_step(void){uint32_t generation=7;unsigned mute_epoch=0;do{
''' + body + r'''
}while(0);}
int main(void){
    int16_t memory[1000],history[173],live[294];
    for(int i=0;i<173;i++)history[i]=i;
    for(int i=0;i<294;i++)live[i]=1000+i;
    starter_preroll_init(&s_preroll,memory,1000);
    starter_preroll_append(&s_preroll,history,173,1000);
    s_expected_wake_token=starter_preroll_prepare(&s_preroll,1000,1000);
    assert(starter_preroll_bind(&s_preroll,s_expected_wake_token,7,1000));
    capture_step();
    assert(packets==1 && packet_samples==26 && s_expected_wake_token==0);
    clean.pcm_16k=live;clean.samples_16k=294;capture_step();
    assert(packets==2 && packet_samples==0);
    assert(timestamps[1]-timestamps[0]==20);
    assert(sent[0][0]==0 && sent[0][318]==159 && sent[0][319]==159);
    assert(sent[1][0]==160 && sent[1][24]==172 && sent[1][25]==172);
    assert(sent[1][26]==1000 && sent[1][319]==1293);
}
''')

    def test_uplink_selects_codec_and_preserves_20ms_frames(self):
        run_c(COMMON + r'''
typedef struct {int mode;uint32_t generation,timestamp_ms;unsigned mute_epoch;
    size_t sample_count;int16_t pcm[320];} audio_tx_item_t;
static audio_tx_item_t pool[1],*s_audio_tx_pool=pool;
static int s_audio_tx_ready_queue=1,s_audio_tx_free_queue=2;
static atomic_uint s_mute_epoch,s_audio_sent;
static atomic_bool s_microphone_muted,s_uplink_enabled=true,s_room_pressed;
static jmp_buf done;
static unsigned reads,opus_enc,alaw_enc,opus_send,alaw_send;
static size_t encoded_size=37;
static int64_t clock_us;
static int64_t esp_timer_get_time(void){return clock_us;}
static bool starter_tirtc_audio_ready(void){return true;}
static int xQueueReceive(int q,uint8_t *slot,int wait){
    if(reads++) {longjmp(done,1);} *slot=0;return pdTRUE;
}
static int xQueueSend(int q,uint8_t *slot,int wait){return pdTRUE;}
static int encode(esp_audio_enc_in_frame_t *in,esp_audio_enc_out_frame_t *out,bool opus){
    clock_us+=opus ? 3000 : 800;
    assert(in->len==(opus ? 640U : 320U));
    assert(((int16_t *)in->buffer)[0]==42);
    out->encoded_bytes=opus ? encoded_size : 160;return codec_error;
}
static int esp_g711_enc_process(void *h,esp_audio_enc_in_frame_t *i,esp_audio_enc_out_frame_t *o){
    alaw_enc++;return encode(i,o,false);
}
static int esp_opus_enc_process(void *h,esp_audio_enc_in_frame_t *i,esp_audio_enc_out_frame_t *o){
    opus_enc++;return encode(i,o,true);
}
static int starter_tirtc_send_alaw(uint32_t ts,const void *data,uint32_t size){
    clock_us+=25000;
    assert(mode!=STARTER_TIRTC_AI && ts==100 && size==160);alaw_send++;return 0;
}
static int starter_tirtc_send_opus(uint32_t ts,const void *data,uint32_t size){
    clock_us+=25000;
    assert(mode==STARTER_TIRTC_AI && ts==100 && size==encoded_size);opus_send++;return 0;
}
''' + function(MEDIA.read_text(), 'update_max_counter') + function(MEDIA.read_text(), 'update_timing_average') + function(MEDIA.read_text(), 'uplink_allowed') + function(MEDIA.read_text(), 'audio_uplink_task') + r'''
static void replay(void){
    reads=0;pool[0]=(audio_tx_item_t){.mode=mode,.generation=active_generation,.timestamp_ms=100,
        .sample_count=mode==STARTER_TIRTC_AI ? 320 : 160};pool[0].pcm[0]=42;
    if(setjmp(done)==0) audio_uplink_task(NULL);
}
int main(void){
    for(int i=0;i<100;i++){
        active_generation=7+i;
        mode=STARTER_TIRTC_AI;encoded_size=37+i;replay();
        mode=STARTER_TIRTC_H5;replay();
    }
    assert(opus_enc==100 && alaw_enc==100 && opus_send==100 && alaw_send==100 && resets==100);
    assert(atomic_load(&s_audio_encode_avg_us)==1900);
    assert(atomic_load(&s_audio_encode_max_us)==3000);
    mode=STARTER_TIRTC_AI;codec_error=-1;replay();assert(opus_send==100);
    codec_error=0;s_microphone_muted=true;replay();assert(opus_enc==101);
}
''')

    def test_downlink_opus_is_played_at_16k_without_double_upsampling(self):
        run_c(COMMON + r'''
typedef struct {int mode;uint32_t generation;struct {size_t length;} frame;
    uint8_t payload[1500];} audio_rx_item_t;
static int16_t s_decode_pcm[1500],s_play_stereo[6000];
static uint32_t s_playback_resampler_generation;
static int16_t s_playback_previous;
static atomic_uint s_audio_decode_failed,s_audio_decoded,s_audio_played,
    s_audio_write_failed,s_audio_playback_blocked;
static atomic_bool s_speaker_muted;
static int s_audio_output_mutex;
static bool s_amp_enabled=true;
static struct {unsigned playback_channels;} s_board_audio_format={2};
static unsigned opus_dec,alaw_dec,writes;
static size_t alaw_samples=160;
static int64_t clock_us;
static atomic_uint_fast32_t s_audio_decode_max_us,s_audio_lock_max_us,
    s_audio_write_max_us,s_audio_pcm_last_samples;
static int64_t esp_timer_get_time(void){return clock_us;}
static int decode(esp_audio_dec_out_frame_t *o,esp_audio_dec_info_t *info,bool opus){
    clock_us+=opus ? 3000 : 5000;
    size_t samples=opus ? 320 : alaw_samples;assert(o->len>=samples*2);
    for(size_t i=0;i<samples;i++)((int16_t *)o->buffer)[i]=(int16_t)(100+i);
    o->decoded_size=samples*2;info->sample_rate=opus ? 16000 : 8000;
    info->channel=1;info->bits_per_sample=16;return codec_error;
}
static int esp_g711a_dec_decode(void *h,esp_audio_dec_in_raw_t *i,esp_audio_dec_out_frame_t *o,esp_audio_dec_info_t *n){alaw_dec++;return decode(o,n,false);}
static int esp_opus_dec_decode(void *h,esp_audio_dec_in_raw_t *i,esp_audio_dec_out_frame_t *o,esp_audio_dec_info_t *n){opus_dec++;return decode(o,n,true);}
static int xSemaphoreTake(int h,int t){assert(s_play_stereo[0]==0x5555);clock_us+=1000;return pdTRUE;}
static void xSemaphoreGive(int h){}
static int write_pcm(void *context,const int16_t *pcm,size_t frames,size_t *written){
    clock_us+=20000;
    assert(frames==(mode==STARTER_TIRTC_AI ? 320 : alaw_samples*2));
    assert(pcm[0]==100 && pcm[1]==100);
    if(mode==STARTER_TIRTC_AI)assert(pcm[2]==101 && pcm[3]==101);
    else assert(pcm[4]==100 && pcm[5]==100 && pcm[6]==101 && pcm[7]==101);
    *written=frames*4;writes++;return 0;
}
static struct {void *context;int (*write_pcm)(void *,const int16_t *,size_t,size_t *);} adapter={NULL,write_pcm},*s_audio_adapter=&adapter;
''' + function(MEDIA.read_text(), 'update_max_counter') + function(MEDIA.read_text(), 'update_timing_average') + function(MEDIA.read_text(), 'play_audio_item') + r'''
static bool play(audio_rx_item_t *item){memset(s_play_stereo,0x55,sizeof(s_play_stereo));return play_audio_item(item);}
int main(void){
    audio_rx_item_t item={.generation=7,.frame.length=37};
    for(int i=0;i<100;i++){
        item.generation=active_generation=7+i;
        mode=item.mode=STARTER_TIRTC_AI;assert(play(&item));
        mode=item.mode=STARTER_TIRTC_H5;assert(play(&item));
    }
    assert(opus_dec==100 && alaw_dec==100 && writes==200 && resets==100);
    assert(atomic_load(&s_audio_decode_max_us)==5000);
    assert(atomic_load(&s_audio_lock_max_us)==1000);
    assert(atomic_load(&s_audio_write_max_us)==20000);
    assert(atomic_load(&s_audio_pcm_last_samples)==160);
    assert(atomic_load(&s_audio_decode_avg_us)==4000);
    assert(atomic_load(&s_audio_lock_avg_us)==1000);
    assert(atomic_load(&s_audio_write_avg_us)==20000);
    mode=item.mode=STARTER_TIRTC_AI;codec_error=-1;
    assert(!play(&item) && writes==200);
    codec_error=0;s_speaker_muted=true;assert(!play(&item) && writes==200);
    item.generation=6;assert(!play(&item));
    s_speaker_muted=false;mode=item.mode=3; /* VoIP: 40ms packets as on BK. */
    item.generation=active_generation=200;alaw_samples=320;
    assert(play(&item) && writes==201);
}
''')


if __name__ == '__main__':
    unittest.main()
