"""Replay ordered WeChat shortcuts and call-ring lifecycle at BK public seams."""
import unittest
from test_s3_product_regressions import ROOT, function, run_c

APP = ROOT/'platforms/beken/runtime/src/xiaotai_app.c'
AUDIO = ROOT/'platforms/beken/runtime/src/xiaotai_audio.c'


class BkCallRingtone(unittest.TestCase):
    def test_shortcut_skips_devices_and_preserves_wechat_order(self):
        app = APP.read_text()
        contacts = (ROOT/'product/src/xiaotai_contacts.c').read_text()
        run_c(r'''
#include <assert.h>
#include <string.h>
#include "xiaotai_contacts.h"
#define BK_LOGI(...) ((void)0)
#define BK_LOGW(...) ((void)0)
static xiaotai_contacts_t s_contacts;
static int dialed,guide;
static void dial_contact(const xiaotai_contact_t *c){
 assert(c->type==XIAOTAI_CONTACT_VOIP && strcmp(c->name,"wx-first")==0);dialed++;
}
static void show_wechat_qr_waiting(bool wechat){assert(wechat);guide++;}
static void quick_call_first_wechat_contact(void);
''' + function(contacts,'xiaotai_contacts_first') +
            function(contacts,'xiaotai_contacts_first_of_type') +
            function(app,'quick_call_first_contact') +
            function(app,'quick_call_first_wechat_contact') + r'''
int main(void){
 s_contacts.count=4;
 s_contacts.entries[0].type=XIAOTAI_CONTACT_DEVICE;
 s_contacts.entries[1].type=XIAOTAI_CONTACT_VOIP;strcpy(s_contacts.entries[1].name,"wx-first");
 s_contacts.entries[2].type=XIAOTAI_CONTACT_DEVICE;
 s_contacts.entries[3].type=XIAOTAI_CONTACT_VOIP;strcpy(s_contacts.entries[3].name,"wx-second");
 quick_call_first_contact();quick_call_first_wechat_contact();assert(dialed==2 && guide==0);
 s_contacts.count=1;quick_call_first_contact();assert(dialed==2 && guide==1);
 s_contacts.count=0;quick_call_first_contact();assert(dialed==2 && guide==2);
}
''', [ROOT/'product/include'])

    def test_runtime_rings_only_for_waiting_calls_and_stops_on_every_exit(self):
        app = APP.read_text()
        service = function(app,'service_call_ringtone')
        call_state=(ROOT/'product/src/xiaotai_call_state.c').read_text()
        lifecycle='\n'.join(function(call_state,n) for n in
            ('copy_value','xiaotai_call_state_begin_voip_outbound','xiaotai_call_state_accept_voip'))
        run_c(r'''
#include <assert.h>
#include <string.h>
#include "xiaotai_runtime.h"
#include "xiaotai_call_state.h"
#include "xiaotai_audio.h"
static xiaotai_runtime_t s_runtime;
static xiaotai_call_state_t s_calls;
static int s_runtime_mutex;
static xiaotai_call_ring_t kind;
static void rtos_lock_mutex(int *m){}
static void rtos_unlock_mutex(int *m){}
bool xiaotai_runtime_has_incoming(const xiaotai_runtime_t *r){return r->pending_incoming!=0;}
void xiaotai_audio_service_call_ringtone(xiaotai_call_ring_t k){kind=k;}
''' + lifecycle + service + r'''
int main(void){
 service_call_ringtone();assert(kind==XIAOTAI_CALL_RING_NONE);
 s_runtime.owner=XIAOTAI_OWNER_WECHAT_VOIP;s_runtime.state=XIAOTAI_STATE_CALL_CONNECTING;
 assert(xiaotai_call_state_begin_voip_outbound(&s_calls,"wx-user"));
 service_call_ringtone();assert(kind==XIAOTAI_CALL_RING_OUTGOING);
 /* Replay the real descriptor callback: outbound means request-pending, NOT caller role. */
 assert(xiaotai_call_state_accept_voip(&s_calls,"peer","token","room","call",true));
 assert(!s_calls.voip.outbound);
 for(unsigned i=0;i<100;i++){service_call_ringtone();assert(kind==XIAOTAI_CALL_RING_OUTGOING);}
 s_runtime.state=XIAOTAI_STATE_CALL_ACTIVE;service_call_ringtone();assert(kind==XIAOTAI_CALL_RING_NONE);
 memset(&s_calls,0,sizeof(s_calls));s_runtime.state=XIAOTAI_STATE_CALL_CONNECTING;
 service_call_ringtone();assert(kind==XIAOTAI_CALL_RING_NONE);
 s_runtime.state=XIAOTAI_STATE_CALL_INCOMING;service_call_ringtone();assert(kind==XIAOTAI_CALL_RING_INCOMING);
 const int exits[]={XIAOTAI_STATE_CALL_ACTIVE,XIAOTAI_STATE_CALL_ENDING,XIAOTAI_STATE_WAITING};
 for(unsigned i=0;i<3;i++){s_runtime.state=exits[i];service_call_ringtone();assert(kind==XIAOTAI_CALL_RING_NONE);}
 s_runtime.owner=XIAOTAI_OWNER_AI;s_runtime.state=XIAOTAI_STATE_AI_ACTIVE;
 s_runtime.pending_incoming=XIAOTAI_OWNER_WECHAT_VOIP;
 service_call_ringtone();assert(kind==XIAOTAI_CALL_RING_INCOMING);
 s_runtime.pending_incoming=0;service_call_ringtone();assert(kind==XIAOTAI_CALL_RING_NONE);
}
''',[ROOT/'product/include',ROOT/'platforms/beken/runtime/include'])

    def test_descriptor_callback_keeps_ringback_while_transport_connects(self):
        source = function(APP.read_text(),'handle_decoded_platform_signal')
        connection=source.index('int rc = xiaotai_tirtc_voip_connect(')
        start=source.rfind('if (outbound) {',0,connection)
        end=source.index('} else if (accepted)',connection)+1
        branch=source[start:end]
        run_c(r'''
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include "xiaotai_call_state.h"
#include "xiaotai_audio.h"
#define BK_LOGI(...) ((void)0)
static xiaotai_call_state_t s_calls;
static xiaotai_call_ring_t ring=XIAOTAI_CALL_RING_OUTGOING;
static int connections;
void xiaotai_audio_service_call_ringtone(xiaotai_call_ring_t kind){ring=kind;}
static int xiaotai_tirtc_voip_connect(const char *p,const char *t,uint32_t g){connections++;return 0;}
static void finish_voip(uint32_t g,int e){assert(0);}
static void xiaotai_ui_show_status(const char *s){}
int main(void){bool outbound=true;uint32_t generation=2;
''' + branch + r'''
 assert(connections==1 && ring==XIAOTAI_CALL_RING_OUTGOING);
}
''',[ROOT/'product/include',ROOT/'platforms/beken/runtime/include'])

    def test_audio_prompt_ownership_retry_mute_and_live_mix(self):
        source = AUDIO.read_text()
        names = ('audio_control_enter','audio_control_leave','call_ringtone_pcm','service_call_ringtone_locked','xiaotai_audio_service_call_ringtone','mix_call_ringtone')
        run_c(r'''
#include <assert.h>
#include <stdlib.h>
#include <string.h>
#include <stdatomic.h>
#include "xiaotai_audio.h"
#include "xiaotai_prompt_assets.h"
#define BK_OK 0
#define BK_LOGI(...) ((void)0)
static atomic_uint s_volume=5,s_prompt_generation,s_call_ring_started_ms;
static atomic_bool s_speaker_muted,s_prompt_active;
static atomic_int s_call_ring_kind;
static bool s_running;
static atomic_flag s_audio_control_lock=ATOMIC_FLAG_INIT;
#define AUDIO_TASK_STOP_TIMEOUT_MS 10
static void rtos_delay_milliseconds(unsigned n){}
static unsigned s_ring_prompt_generation,s_ring_prompt_volume;
static unsigned now,starts,cancels,allocations;
static bool fail_alloc,fail_start,cancel_timeout;
static int16_t *owned;
static unsigned rtos_get_time(void){return now;}
static void *psram_malloc(size_t size){if(fail_alloc)return NULL;allocations++;return malloc(size);}
static void psram_free(void *p){assert(p && allocations);allocations--;free(p);}
void xiaotai_audio_cancel_prompt(void){cancels++;if(cancel_timeout)return;
 if(owned){psram_free(owned);owned=NULL;}s_prompt_active=false;}
static void cancel_prompt_locked(void){xiaotai_audio_cancel_prompt();}
static int start_prompt_locked(int16_t *pcm,size_t n,unsigned repeats){
 assert(n>=2000 && repeats==1 && !s_running && !s_prompt_active);
 if(fail_start)return -1;
 starts++;s_prompt_generation++;owned=pcm;s_prompt_active=true;return 0;
}
''' + '\n'.join(function(source,n) for n in names) + r'''
int main(void){
 xiaotai_audio_service_call_ringtone(XIAOTAI_CALL_RING_OUTGOING);
 assert(starts==1 && allocations==1 && s_call_ring_kind==XIAOTAI_CALL_RING_OUTGOING);
 xiaotai_audio_service_call_ringtone(XIAOTAI_CALL_RING_OUTGOING);assert(starts==1);
 xiaotai_audio_service_call_ringtone(XIAOTAI_CALL_RING_INCOMING);
 assert(starts==2 && cancels==1 && allocations==1);
 /* Muting must stop a currently ringing idle speaker and unmute can retry. */
 s_speaker_muted=true;xiaotai_audio_service_call_ringtone(XIAOTAI_CALL_RING_INCOMING);
 assert(!s_prompt_active && allocations==0);
 s_speaker_muted=false;fail_alloc=true;
 xiaotai_audio_service_call_ringtone(XIAOTAI_CALL_RING_INCOMING);assert(starts==2);
 fail_alloc=false;fail_start=true;xiaotai_audio_service_call_ringtone(XIAOTAI_CALL_RING_INCOMING);
 assert(allocations==0);fail_start=false;
 xiaotai_audio_service_call_ringtone(XIAOTAI_CALL_RING_INCOMING);assert(starts==3);
 /* A delayed stop retains ownership; the next service retries cancellation. */
 cancel_timeout=true;xiaotai_audio_service_call_ringtone(XIAOTAI_CALL_RING_NONE);
 assert(s_prompt_active);cancel_timeout=false;
 xiaotai_audio_service_call_ringtone(XIAOTAI_CALL_RING_NONE);
 assert(!s_prompt_active && allocations==0);
 /* Stopping rings must never cancel a later binding/acknowledgement prompt. */
 s_prompt_generation++;s_prompt_active=true;unsigned before=cancels;
 xiaotai_audio_service_call_ringtone(XIAOTAI_CALL_RING_NONE);assert(cancels==before && s_prompt_active);
 s_prompt_active=false;s_running=true;now=100;
 xiaotai_audio_service_call_ringtone(XIAOTAI_CALL_RING_INCOMING);assert(starts==3 && !s_prompt_active);
 int16_t pcm[320]={0};now=300;mix_call_ringtone(pcm,320,16000);
 bool audible=false;for(unsigned i=0;i<320;i++){assert(pcm[i]==xiaotai_ring_incoming[1600+i/2]);audible|=pcm[i]!=0;}
 assert(audible);
 xiaotai_audio_service_call_ringtone(XIAOTAI_CALL_RING_NONE);
 memset(pcm,42,sizeof(pcm));int16_t old=pcm[0];mix_call_ringtone(pcm,320,16000);assert(pcm[0]==old);
 xiaotai_audio_service_call_ringtone(XIAOTAI_CALL_RING_OUTGOING);
 assert(s_call_ring_kind==XIAOTAI_CALL_RING_NONE && !s_prompt_active);
}
''',[ROOT/'platforms/beken/runtime/include',ROOT/'platforms/beken/runtime/assets'])

    def test_media_handoff_cannot_restart_ring_before_running_is_published(self):
        source = AUDIO.read_text()
        locked = 'static int audio_start_duplex_locked(' in source
        name = 'audio_start_duplex_locked' if locked else 'xiaotai_audio_start_duplex'
        start = function(source, name)
        start = start[:start.index('    if (!s_ring_mutex_ready)')] + r'''
 handoff_hook();s_running=true;return BK_OK;
}
'''
        helpers = ''
        if locked:
            helpers = '\n'.join(function(source,n) for n in
                ('audio_control_enter','audio_control_leave'))
        service = ''
        if locked:
            service = function(source,'service_call_ringtone_locked')
        service += function(source,'xiaotai_audio_service_call_ringtone')
        run_c(r'''
#include <assert.h>
#include <stdlib.h>
#include <string.h>
#include <stdatomic.h>
#include "xiaotai_audio.h"
#include "xiaotai_prompt_assets.h"
#define BK_OK 0
#define BK_ERR_BUSY -2
#define BK_ERR_PARAM -3
#define BK_LOGI(...) ((void)0)
#define AUDIO_TASK_STOP_TIMEOUT_MS 10
static atomic_flag s_audio_control_lock=ATOMIC_FLAG_INIT;
static atomic_bool s_running,s_speaker_muted,s_prompt_active;
static atomic_int s_call_ring_kind=XIAOTAI_CALL_RING_OUTGOING;
static atomic_uint s_volume=5,s_prompt_generation,s_call_ring_started_ms;
static unsigned s_ring_prompt_generation,s_ring_prompt_volume,starts;
static unsigned rtos_get_time(void){return 0;}
static void rtos_delay_milliseconds(unsigned n){}
static void *psram_malloc(size_t n){return malloc(n);}
static void psram_free(void *p){free(p);}
void xiaotai_audio_cancel_prompt(void){s_prompt_active=false;}
static void cancel_prompt_locked(void){xiaotai_audio_cancel_prompt();}
static int start_prompt_locked(int16_t *p,size_t n,unsigned r){starts++;free(p);return 0;}
static int start_prompt(int16_t *p,size_t n,unsigned r){return start_prompt_locked(p,n,r);}
static void handoff_hook(void);
''' + helpers + function(source,'call_ringtone_pcm') + service + start +
            (function(source,'xiaotai_audio_start_duplex') if locked else '') + r'''
static void handoff_hook(void){
 /* Replay control maintenance after prompt cancellation, before media becomes running. */
 xiaotai_audio_service_call_ringtone(XIAOTAI_CALL_RING_OUTGOING);
 assert(starts==0 && s_call_ring_kind==XIAOTAI_CALL_RING_NONE);
}
static int uplink(const uint8_t *data,size_t size,uint32_t timestamp,void *context){return 0;}
int main(void){
 (void)s_audio_control_lock;
 assert(xiaotai_audio_start_duplex(XIAOTAI_AUDIO_CODEC_ALAW_8K,
          XIAOTAI_AUDIO_CODEC_ALAW_8K,uplink,NULL)==BK_OK);
 xiaotai_audio_service_call_ringtone(XIAOTAI_CALL_RING_OUTGOING);
 assert(starts==0 && s_call_ring_kind==XIAOTAI_CALL_RING_NONE);
}
''',[ROOT/'platforms/beken/runtime/include',ROOT/'platforms/beken/runtime/assets'])

    def test_live_playback_rings_in_silence_without_counting_network_frames(self):
        source = AUDIO.read_text()
        run_c(r'''
#include <assert.h>
#include <string.h>
#include <stdatomic.h>
#include "xiaotai_audio.h"
#include "xiaotai_prompt_assets.h"
#define BK_LOGI(...) ((void)0)
#define BK_LOGW(...) ((void)0)
#define BK_OK 0
#define AUDIO_ALAW_FRAME_BYTES 160
#define AUDIO_RING_FRAMES 8
#define OPUS_MAX_PACKET_BYTES 512
#define OPUS_MAX_DECODE_SAMPLES 960
#define OPUS_PACKET_SLOTS 64
static atomic_uint s_volume=10,s_playback_columns,s_playback_columns_at_ms,s_last_downlink_ms;
static atomic_uint s_playback_pcm_ms,s_playback_tail_ms,s_downlink_frames,s_call_ring_started_ms;
static atomic_int s_call_ring_kind=XIAOTAI_CALL_RING_INCOMING;
static atomic_bool s_speaker_muted,s_playback_in_progress;
static bool s_play_run;
static xiaotai_audio_codec_t s_codec;
static size_t s_frame_samples;
static void *s_opus_decoder,*s_play_thread;
static int s_play_stopped,writes,refs,packet;
typedef int opus_int32;
typedef int beken_thread_arg_t;
static unsigned rtos_get_time(void){return 200;}
static size_t opus_ring_take(void *p,size_t n){if(packet){memset(p,0,n);return 5;}return 0;}
static int opus_decode(void *d,void *p,int n,int16_t *pcm,int max,int fec){memset(pcm,0,320*2);return 320;}
static size_t ring_take(void *p,size_t n){if(packet){memset(p,0,n);return n;}return 0;}
static int16_t alaw2linear(unsigned char c){return 0;}
static void rtos_delay_milliseconds(unsigned n){assert(n==5);s_play_run=false;}
static void rtos_set_semaphore(int *s){}
static void rtos_delete_thread(void *t){}
static unsigned playback_columns_pack(void *p,size_t n){return 0;}
static unsigned xiaotai_audio_playback_tail_after_write(unsigned a,unsigned b,unsigned c){return 0;}
static void aec_reference_push(const int16_t *pcm,size_t n){refs++;}
static int write_pcm(void *c,const int16_t *pcm,size_t n,size_t *bytes){
 bool audible=false;for(size_t i=0;i<n;i++)audible|=pcm[i]!=0;
 assert(audible==!s_speaker_muted);*bytes=n*2;writes++;s_play_run=false;return 0;
}
static struct {void *context;int (*write_pcm)(void *,const int16_t *,size_t,size_t *);} board={0,write_pcm},*s_board_audio=&board;
''' + function(source,'call_ringtone_pcm') + function(source,'mix_call_ringtone') +
            function(source,'playback_task') + r'''
int main(void){
 for(int codec=0;codec<2;codec++)for(int mute=0;mute<2;mute++)for(packet=0;packet<2;packet++){
  s_codec=codec;s_frame_samples=codec?320:160;s_speaker_muted=mute;
  s_play_run=true;s_downlink_frames=0;s_playback_pcm_ms=0;writes=refs=0;
  playback_task(0);assert(writes==1 && refs==1);
  assert(s_downlink_frames==(unsigned)packet && s_playback_pcm_ms==(packet?20U:0U));
 }
 s_call_ring_kind=XIAOTAI_CALL_RING_NONE;packet=0;s_play_run=true;writes=0;
 playback_task(0);assert(writes==0);
}
''',[ROOT/'platforms/beken/runtime/include',ROOT/'platforms/beken/runtime/assets'])


if __name__ == '__main__':
    unittest.main()
