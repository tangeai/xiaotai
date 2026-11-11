"""Replay the BK media worker's real disconnect boundary against audio/SDK fakes."""
import re
import unittest
from test_s3_product_regressions import ROOT, function, run_c

RTC = ROOT / 'platforms/beken/runtime/src/xiaotai_tirtc.c'


class BKDisconnectPlayback(unittest.TestCase):
    def replay(self, scenario):
        source = RTC.read_text()
        worker = function(source, 'media_worker')
        branch = worker[worker.index('if (event.type == MEDIA_EVENT_DISCONNECT)'):worker.index('if (event.type == MEDIA_EVENT_CONNECTED)')]
        closed = worker[worker.index('if (event.type == MEDIA_EVENT_CLOSED)'):worker.index('if (event.mode == CONNECTION_AI &&\n                s_handlers.on_ai_disconnected')]
        cancel = function(source, 'xiaotai_tirtc_ai_cancel_tail')
        constants = '\n'.join(re.findall(r'^#define AI_CLOSE_.*$', source, re.M))
        run_c(r'''
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include <stdatomic.h>
#define BK_LOGI(...) ((void)0)
#define BK_LOGD(...) ((void)0)
#define CONNECTION_STREAM 1
#define CONNECTION_AI 2
#define CONNECTION_ROOM 5
#define MEDIA_EVENT_DISCONNECT 1
#define MEDIA_EVENT_CLOSED 2
typedef void *tirtc_conn_t;
typedef struct {int type,mode,error;unsigned generation;tirtc_conn_t connection;} media_event_t;
static media_event_t s_pending_close_event;
static atomic_int s_pending_close_state;
static atomic_uint s_ai_cancel_tail_generation;
static void xiaotai_tirtc_ai_cancel_tail(uint32_t generation);
static unsigned cancel_at_ms;
static tirtc_conn_t current=(void *)1;
static unsigned pending,written,clock_ms,stops,disconnects,drain_checks;
static bool running=true,uplink=true,stuck;
static bool connection_matches(tirtc_conn_t c){return c==current;}
static bool xiaotai_audio_running(void){return running;}
static bool xiaotai_audio_playback_is_drained(unsigned quiet){(void)quiet;drain_checks++;return pending==0;}
static void xiaotai_audio_playback_boundary(bool ended){assert(ended);}
static void xiaotai_audio_log_playback_status(const char *s){(void)s;}
static void xiaotai_audio_set_uplink_enabled(bool enabled){uplink=enabled;}
static unsigned rtos_get_time(void){return clock_ms;}
static void rtos_delay_milliseconds(unsigned ms){
    assert(running && !uplink);clock_ms+=ms;
    if(cancel_at_ms && clock_ms>=cancel_at_ms){xiaotai_tirtc_ai_cancel_tail(7);cancel_at_ms=0;}
    if(pending && !stuck && clock_ms%20==0){pending--;written++;}
}
static int xiaotai_video_stop(void){return 0;}
static int xiaotai_audio_stop(void){running=false;pending=0;stops++;return 0;}
static int TiRtcDisconnect(tirtc_conn_t c){
    if(c==current)assert(!running && "capture must stop before SDK frees connection");
    disconnects++;return 0;
}
''' + constants + '\n' + cancel + function(source, 'drain_ai_playback_after_transport_close') +
              '\nstatic void dispatch(media_event_t event){\n' + branch.replace('continue;', 'return;') +
              '}\nstatic void closed_event(media_event_t event){\n' + closed +
              '}}\nint main(void){\n' + scenario + '\n}\n')

    def test_remote_close_plays_received_tail_before_stop(self):
        self.replay(r'''
    written=303;pending=46; /* log replay: one decoder-owned frame plus 45 queued */
    dispatch((media_event_t){.type=1,.mode=2,.error=-40008,.connection=current});
    assert(written==349 && "remote close must preserve all received AI frames");
    assert(stops==1 && disconnects==1 && clock_ms==920);
    unsigned checks=drain_checks;
    closed_event((media_event_t){.type=MEDIA_EVENT_CLOSED,.mode=2});
    assert(checks==drain_checks && "stopped audio must not produce a false drained report");
''')

    def test_stalled_playback_has_bounded_exit(self):
        self.replay(r'''
    pending=46;stuck=true;
    dispatch((media_event_t){.type=1,.mode=2,.error=-40008,.connection=current});
    assert(clock_ms==AI_CLOSE_DRAIN_TIMEOUT_MS && stops==1 && disconnects==1);
''')

    def test_remote_closed_callback_without_end_session_drains(self):
        self.replay(r'''
    written=242;pending=7;
    closed_event((media_event_t){.type=MEDIA_EVENT_CLOSED,.mode=2,.error=0});
    assert(written==249 && "normal transport close must finish received audio");
    assert(stops==1 && clock_ms==140 && !running && !uplink);
''')

    def test_user_hangup_and_room_close_are_immediate(self):
        self.replay(r'''
    pending=46;
    dispatch((media_event_t){.type=1,.mode=2,.error=0,.connection=current});
    assert(clock_ms==0 && written==0 && stops==1 && drain_checks==0);
    running=true;pending=46;
    dispatch((media_event_t){.type=1,.mode=5,.error=-40008,.connection=current});
    assert(clock_ms==0 && written==0 && stops==2 && drain_checks==0);
''')

    def test_local_stop_marks_cancel_before_signalling(self):
        source=(ROOT/'platforms/beken/runtime/src/xiaotai_app.c').read_text()
        run_c(r'''
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#define XIAOTAI_AI_COMMAND 0x2100
static unsigned cancelled,sends,disconnects,finishes;
static bool connected=true;
static void xiaotai_tirtc_ai_cancel_tail(uint32_t g){cancelled=g;}
static bool xiaotai_tirtc_connected(void){return connected;}
static int xiaotai_tirtc_send_command(unsigned cmd,const void *p,unsigned n){assert(cancelled==7);sends++;return 0;}
static int xiaotai_tirtc_disconnect(void){assert(cancelled==7);disconnects++;return 0;}
static void finish_ai(unsigned g,int error){assert(cancelled==g);finishes++;}
static void xiaotai_ui_show_status(const char *s){}
''' + function(source,'stop_ai') + r'''
int main(void){
 stop_ai(7);assert(sends==1 && disconnects==1 && finishes==0);
 connected=false;cancelled=0;stop_ai(7);
 assert(cancelled==7 && sends==1 && disconnects==2 && finishes==1);
}
''')
        self.assertIn('xiaotai_tirtc_ai_cancel_tail(event->generation)',function(source,'handle_ai_call_intent'))
        self.assertIn('stop_ai(generation)',function(source,'request_accept_pending'))

    def test_user_cancel_overrides_remote_tail_drain(self):
        self.replay(r'''
    pending=46;
    xiaotai_tirtc_ai_cancel_tail(7);
    dispatch((media_event_t){.type=1,.mode=2,.error=-40008,.generation=7,.connection=current});
    assert(clock_ms==0 && written==0 && stops==1);
    /* Cancel on the control core while media worker is already draining. */
    s_ai_cancel_tail_generation=0;running=true;pending=46;cancel_at_ms=30;
    dispatch((media_event_t){.type=1,.mode=2,.error=-40008,.generation=7,.connection=current});
    assert(clock_ms==30 && written==1 && stops==2);
    /* Connection callback may have already cleared the handle. */
    running=true;pending=46;clock_ms=0;written=0;
    closed_event((media_event_t){.type=MEDIA_EVENT_CLOSED,.mode=2,.generation=7});
    assert(clock_ms==0 && written==0 && stops==3);
    /* Old cancellation must not truncate the next generation's normal tail. */
    running=true;pending=2;
    closed_event((media_event_t){.type=MEDIA_EVENT_CLOSED,.mode=2,.generation=8});
    assert(clock_ms==40 && written==2);
''')

    def test_stale_cleanup_preserves_current_audio(self):
        self.replay(r'''
    pending=46;
    dispatch((media_event_t){.type=1,.mode=2,.error=-40008,.connection=(void *)2});
    assert(running && uplink && pending==46 && stops==0 && drain_checks==0);
    assert(disconnects==1);
''')


if __name__ == '__main__':
    unittest.main()
