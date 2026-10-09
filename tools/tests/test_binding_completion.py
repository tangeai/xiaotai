"""Replay production binding playback, including ACK during audio and gaps."""
import unittest
from test_s3_product_regressions import ROOT, function, run_c

class BindingCompletion(unittest.TestCase):
    def test_ack_stops_repeats_without_waiting_for_voice(self):
        source = (ROOT / 'platforms/esp-idf/main/app_main.c').read_text()
        run_c(r'''
#include <assert.h>
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include <stdatomic.h>
typedef int esp_err_t;
#define ESP_OK 0
#define ESP_ERR_INVALID_STATE 1
#define VERIFICATION_PROMPT_REPEAT_COUNT 3
#define VERIFICATION_PROMPT_GAP_MS 700
#define pdMS_TO_TICKS(x) (x)
#define ESP_LOGI(...) ((void)0)
static atomic_bool s_verification_prompt_cancelled;
static unsigned epoch, plays, delays, trigger;
static uint32_t starter_media_playback_epoch(void){return epoch;}
static void starter_media_cancel_pcm8k_playback(void){epoch++;}
static esp_err_t starter_media_play_pcm8k(const int16_t*p,size_t n){
 plays++; if(trigger==1) {atomic_store(&s_verification_prompt_cancelled,true);epoch++;}
 return ESP_OK;
}
static esp_err_t starter_media_play_pcm8k_at_epoch(const int16_t*p,size_t n,uint32_t e){
 assert(e==epoch);return starter_media_play_pcm8k(p,n);
}
static void vTaskDelay(unsigned ms){delays+=ms;if(trigger==2)atomic_store(&s_verification_prompt_cancelled,true);}
''' + function(source,'play_verification_prompt') + r'''
int main(void){int16_t pcm=0;
 trigger=1;assert(play_verification_prompt(&pcm,1,0)==ESP_OK);assert(plays==1 && delays==0);
 plays=delays=0;atomic_store(&s_verification_prompt_cancelled,false);trigger=2;
 assert(play_verification_prompt(&pcm,1,0)==ESP_OK);assert(plays==1 && delays<=20);
 plays=delays=0;atomic_store(&s_verification_prompt_cancelled,true);trigger=0;
 assert(play_verification_prompt(&pcm,1,0)==ESP_OK);assert(plays==0);
 atomic_store(&s_verification_prompt_cancelled,false);
 assert(play_verification_prompt(&pcm,1,0)==ESP_OK);assert(plays==3 && delays==1400);
}
''')

    def test_bk_cancel_during_gap_is_bounded_and_frees_audio(self):
        source = (ROOT / 'firmware/beken/bk7258/lckfb-bk7258-xiaotai/ap/src/xiaotai_audio.c').read_text()
        run_c(r'''
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include <stdatomic.h>
#include <string.h>
#define BK_OK 0
#define BK_LOGI(...) ((void)0)
#define BK_LOGE(...) ((void)0)
#define AUDIO_MAX_FRAME_SAMPLES 160
#define AUDIO_TASK_STOP_TIMEOUT_MS 2000
typedef int beken_thread_arg_t;
static atomic_bool s_prompt_run=true,s_prompt_active=true;
static int s_prompt_stopped;
static void *s_prompt_thread=(void*)1;
static int16_t pcm[320], *s_prompt_pcm=pcm;
static size_t s_prompt_samples=320;
static unsigned writes,delays,stops,frees,signals;
static int xiaotai_board_prompt_audio_start(void){return 0;}
static int xiaotai_board_prompt_audio_write(const int16_t*p,size_t n){writes++;return 0;}
static void xiaotai_board_prompt_audio_stop(void){stops++;}
static void psram_free(void*p){assert(p==pcm);frees++;}
static void rtos_delay_milliseconds(unsigned n){delays+=n;atomic_store(&s_prompt_run,false);}
static void rtos_set_semaphore(int*p){signals++;}
static void rtos_delete_thread(void*p){}
''' + function(source,'play_prompt_pcm') + function(source,'prompt_task') + r'''
int main(void){prompt_task(0);assert(writes==2 && delays<=20 && stops==1 && frees==1 && signals==1);
 assert(!s_prompt_run && !s_prompt_active && !s_prompt_pcm && !s_prompt_thread);}
''')

    def test_only_matching_ack_cancels_and_signals_done(self):
        platform = (ROOT / 'platforms/esp-idf/components/platform_client/src/platform_client.c').read_text()
        branch = platform[platform.index('    } else if (event_id == MQTT_EVENT_PUBLISHED &&'):]
        branch = branch[:branch.index('    } else if (event_id == MQTT_EVENT_ERROR)')]
        branch = branch.replace('    } else if (', '    if (', 1) + '\n    }\n'
        main = (ROOT / 'platforms/esp-idf/main/app_main.c').read_text()
        run_c(r'''
#include <assert.h>
#include <stdbool.h>
#include <stdatomic.h>
#include <stddef.h>
#define MQTT_EVENT_PUBLISHED 1
#define PROVISION_DONE_BIT 1
#define ESP_LOGI(...) ((void)0)
static atomic_bool s_verification_prompt_cancelled;
static unsigned cancels,done;
static void starter_media_cancel_pcm8k_playback(void){cancels++;}
static void xEventGroupSetBits(int events,unsigned bits){assert(events==5 && bits==1);done++;}
''' + function(main, 'cancel_verification_prompt') + r'''
typedef struct {int events,ack_message_id;void(*prompt_cancel_callback)(void*);void*prompt_user_data;} context_t;
typedef struct {int msg_id;} event_t;
static void deliver(context_t *context,event_t *event,int event_id){
''' + branch + r'''
}
int main(void){context_t ctx={5,7,cancel_verification_prompt,0};event_t ev={8};
 deliver(&ctx,&ev,1);assert(!cancels && !done);
 ev.msg_id=7;deliver(&ctx,&ev,0);assert(!cancels && !done);
 deliver(&ctx,&ev,1);assert(cancels==1 && done==1 && s_verification_prompt_cancelled);
 ctx.prompt_cancel_callback=0;deliver(&ctx,&ev,1);assert(cancels==1 && done==2);
}
''')

    def test_success_feedback_follows_persistence(self):
        source = (ROOT / 'platforms/esp-idf/main/app_main.c').read_text()
        run_c(r'''
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
#include <stdatomic.h>
typedef int esp_err_t;
#define ESP_OK 0
#define DISCOVERY_URL "test"
#define ESP_LOGI(...) ((void)0)
static atomic_bool s_verification_prompt_cancelled;
typedef struct {char device_id[65],device_secret[257];} runtime_tirtc_config_t;
static runtime_tirtc_config_t s_tirtc_config;
typedef struct {char device_id[65],device_secret[257];} platform_provision_result_t;
typedef struct {const char *mac_address,*existing_device_id,*existing_device_secret,*discovery_url;unsigned timeout_seconds;esp_err_t(*prompt_callback)(const int16_t*,size_t,void*);void(*prompt_cancel_callback)(void*);void*prompt_user_data;} platform_provision_config_t;
static unsigned grant_error,save_error,saved,feedback;
static esp_err_t play_verification_prompt(const int16_t*p,size_t n,void*u){return 0;}
static void cancel_verification_prompt(void*u){}
static esp_err_t platform_client_provision(const platform_provision_config_t*c,platform_provision_result_t*r){
 assert(c->prompt_cancel_callback==cancel_verification_prompt);return grant_error;
}
static esp_err_t runtime_config_save_tirtc(void*p){saved++;return save_error;}
static void starter_product_binding_saved(void){assert(saved && !save_error);feedback++;}
''' + function(source,'provision_and_save') + r'''
int main(void){grant_error=1;assert(provision_and_save("mac",true)==1 && !saved && !feedback);
 grant_error=0;save_error=2;assert(provision_and_save("mac",true)==2 && saved==1 && !feedback);
 save_error=0;assert(provision_and_save("mac",true)==0 && saved==2 && feedback==1);
}
''')

if __name__ == '__main__': unittest.main()
