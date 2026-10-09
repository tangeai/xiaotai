"""Replay the BK media worker's real disconnect boundary against audio/SDK fakes."""
import re
import unittest
from test_s3_product_regressions import ROOT, function, run_c

RTC = ROOT / 'firmware/beken/bk7258/lckfb-bk7258-xiaotai/ap/src/xiaotai_tirtc.c'


class BKDisconnectPlayback(unittest.TestCase):
    def replay(self, scenario):
        source = RTC.read_text()
        worker = function(source, 'media_worker')
        branch = worker[worker.index('if (event.type == MEDIA_EVENT_DISCONNECT)'):worker.index('if (event.type == MEDIA_EVENT_CONNECTED)')]
        closed = worker[worker.index('if (event.type == MEDIA_EVENT_CLOSED)'):worker.index('if (event.mode == CONNECTION_AI &&\n                s_handlers.on_ai_disconnected')]
        constants = '\n'.join(re.findall(r'^#define AI_CLOSE_.*$', source, re.M))
        run_c(r'''
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include <stdatomic.h>
#define BK_LOGI(...) ((void)0)
#define CONNECTION_AI 2
#define CONNECTION_ROOM 5
#define MEDIA_EVENT_DISCONNECT 1
#define MEDIA_EVENT_CLOSED 2
typedef void *tirtc_conn_t;
typedef struct {int type,mode,error;unsigned generation;tirtc_conn_t connection;} media_event_t;
static tirtc_conn_t current=(void *)1;
static unsigned pending,written,clock_ms,stops,disconnects,drain_checks;
static bool running=true,uplink=true,stuck;
static bool connection_matches(tirtc_conn_t c){return c==current;}
static bool xiaotai_audio_running(void){return running;}
static bool xiaotai_audio_playback_is_drained(unsigned quiet){(void)quiet;drain_checks++;return pending==0;}
static void xiaotai_audio_log_playback_status(const char *s){(void)s;}
static void xiaotai_audio_set_uplink_enabled(bool enabled){uplink=enabled;}
static unsigned rtos_get_time(void){return clock_ms;}
static void rtos_delay_milliseconds(unsigned ms){
    assert(running && !uplink);clock_ms+=ms;
    if(pending && !stuck && clock_ms%20==0){pending--;written++;}
}
static int xiaotai_video_stop(void){return 0;}
static int xiaotai_audio_stop(void){running=false;pending=0;stops++;return 0;}
static int TiRtcDisconnect(tirtc_conn_t c){
    if(c==current)assert(!running && "capture must stop before SDK frees connection");
    disconnects++;return 0;
}
''' + constants + '\n' + function(source, 'drain_ai_playback_after_transport_close') +
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

    def test_user_hangup_and_room_close_are_immediate(self):
        self.replay(r'''
    pending=46;
    dispatch((media_event_t){.type=1,.mode=2,.error=0,.connection=current});
    assert(clock_ms==0 && written==0 && stops==1 && drain_checks==0);
    running=true;pending=46;
    dispatch((media_event_t){.type=1,.mode=5,.error=-40008,.connection=current});
    assert(clock_ms==0 && written==0 && stops==2 && drain_checks==0);
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
