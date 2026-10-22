"""Replay SDK close/accept callbacks and the production media worker."""
import unittest
import re

from test_s3_product_regressions import ROOT, function, run_c


class BKCloseReconnect(unittest.TestCase):
    def test_close_holds_admission_until_media_cleanup_finishes(self):
        project = ROOT / 'platforms/beken/runtime'
        source = (project / 'src/xiaotai_tirtc.c').read_text()
        header = (project / 'include/xiaotai_tirtc.h').read_text()
        types = source[source.index('typedef enum {'):source.index('static beken_queue_t')]
        handlers = header[header.index('typedef struct {\n    /** Runs'):header.index('void xiaotai_tirtc_set_handlers')]
        def definition(name):
            start = re.search(r'^(?:static )?(?:bool|void) ' + name + r'\(',
                              source, re.MULTILINE)
            return function(source[start.start():], name) + '\n'
        functions = ''.join(definition(name) for name in (
            'connection_matches', 'xiaotai_tirtc_connected', 'xiaotai_tirtc_busy',
            'queue_media_event', 'queue_active_disconnect_once',
            'take_pending_close_event', 'media_wait_next_event',
            'xiaotai_tirtc_service_close_wakeup', 'on_conn_accepted',
            'on_disconnected', 'media_worker'))
        run_c(r'''
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <setjmp.h>
#define BK_OK 0
#define BK_FAIL -1
#define TIRTC_E_BUSY -2
#define TIRTC_E_LACK_OF_RESOURCE -3
#define TIRTC_E_INVALID_PARAMETER -4
#define XIAOTAI_AUDIO_CODEC_ALAW_8K 1
#define STREAM_UP_AUDIO_ID 10
#define VOIP_AUDIO_ID 0
#define BEKEN_WAIT_FOREVER 0xffffffffU
#define BK_LOGI(...) ((void)0)
#define BK_LOGD(...) ((void)0)
#define BK_LOGW(...) ((void)0)
#define BK_LOGE(...) ((void)0)
#define os_free free
typedef void *tirtc_conn_t;
typedef void *beken_thread_arg_t;
''' + types + handlers + r'''
static void *s_media_queue = (void *)1;
static atomic_uintptr_t s_connection, s_outgoing_cleanup_connection;
static atomic_uint_fast32_t s_active_generation, s_generation_counter;
static atomic_uint_fast32_t s_pending_outbound_call_generation;
static atomic_int s_connection_error, s_connection_mode, s_pending_close_state;
static atomic_bool s_active_disconnect_queued, s_audio_subscribed, s_video_subscribed;
static atomic_bool s_call_is_caller, s_call_accept_seen, s_voip_accept_seen;
static atomic_bool s_recovery_required, s_outgoing_connect_inflight;
static media_event_t s_pending_close_event, queue[16];
static xiaotai_tirtc_handlers_t s_handlers;
static char s_active_call_room[128];
static unsigned head, tail, notifications, stopped, accepted, disconnected;
static bool fail_push, check_cleanup_busy, in_callback;
static jmp_buf idle;
bool xiaotai_tirtc_busy(void);
static void on_conn_accepted(tirtc_conn_t);
static void on_disconnected(tirtc_conn_t);
static int rtos_push_to_queue(void **q, const media_event_t *e, unsigned timeout) {
    assert(!in_callback && q == &s_media_queue && timeout == 0);
    if (fail_push) { fail_push = false; return BK_FAIL; }
    assert(tail - head < 16); queue[tail++ % 16] = *e; return BK_OK;
}
static int rtos_pop_from_queue(void **q, media_event_t *e, unsigned timeout) {
    assert(q == &s_media_queue && timeout == BEKEN_WAIT_FOREVER);
    if (head == tail) longjmp(idle, 1);
    *e = queue[head++ % 16]; return BK_OK;
}
static void release_outgoing_connect(void) { atomic_store(&s_outgoing_connect_inflight, false); }
static void require_recovery_if_heap_low(const char *reason) { (void)reason; }
static int send_encoded_audio(const uint8_t *p, size_t n, void *ctx) { return 0; }
static int send_h264(const uint8_t *p, size_t n, void *ctx) { return 0; }
static int xiaotai_audio_start(int codec, int (*send)(const uint8_t *, size_t, void *), void *ctx) { return 0; }
static void xiaotai_audio_set_uplink_enabled(bool enabled) { (void)enabled; }
static bool xiaotai_audio_running(void) { return true; }
static int xiaotai_audio_stop(void) {
    if (check_cleanup_busy) {
        assert(xiaotai_tirtc_busy() && "new sessions must wait for audio stop");
        on_conn_accepted((void *)0x2222);
        assert(atomic_load(&s_connection) == 0);
    }
    stopped++; return 0;
}
static void drain_ai_playback_after_transport_close(void) {
    assert(xiaotai_tirtc_busy() && "AI tail drain still owns media");
}
static bool xiaotai_video_running(void) { return false; }
static int xiaotai_video_start(int (*send)(const uint8_t *, size_t, void *), void *ctx) { return 0; }
static int xiaotai_video_stop(void) { return 0; }
static int xiaotai_video_request_keyframe(void) { return 0; }
static int TiRtcDisconnect(tirtc_conn_t c) {
    disconnected++;
    in_callback = true; on_disconnected(c); in_callback = false; return 0;
}
static int TiRtcSubscribeAudio(tirtc_conn_t c, unsigned id) { return 0; }
static int TiRtcSendCommand(tirtc_conn_t c, unsigned cmd, const void *data, unsigned len) { return 0; }
static int normalize_tirtc_result(int rc) { return rc; }
static int subscribe_stream_talkback_audio(tirtc_conn_t c) { return 0; }
static bool stream_connected(uint32_t gen, void *ctx) { accepted++; return true; }
static void closed(uint32_t gen, int error, void *ctx) {
    assert(gen == 7 && error == 0 && stopped > 0);
    assert(!xiaotai_tirtc_busy() && "business notification may immediately dial/answer");
    notifications++;
}
''' + functions + r'''
static void run_worker(void) { if (setjmp(idle) == 0) media_worker(NULL); }
int main(void) {
    s_handlers.on_stream_connected = stream_connected;
    s_handlers.on_stream_disconnected = closed;
    s_handlers.on_ai_disconnected = closed;
    s_handlers.on_call_disconnected = closed;
    s_handlers.on_voip_disconnected = closed;
    s_handlers.on_room_disconnected = closed;
    for (unsigned cycle = 0; cycle < 100; cycle++) {
        for (int mode = CONNECTION_STREAM; mode <= CONNECTION_ROOM; mode++) {
            atomic_store(&s_connection, 0x1111);
            atomic_store(&s_active_generation, 7);
            atomic_store(&s_connection_mode, mode);
            stopped = notifications = accepted = disconnected = 0;
            in_callback = true; on_disconnected((void *)0x1111); in_callback = false;
            assert(!xiaotai_tirtc_connected()); /* Transport really has gone. */
            assert(xiaotai_tirtc_busy() && "close pending must block a new session");
            in_callback = true; on_disconnected((void *)0x1111); in_callback = false;
            assert(!atomic_load(&s_recovery_required)); /* Duplicate close is harmless. */
            on_conn_accepted((void *)0x2222);
            assert(atomic_load(&s_connection) == 0 && accepted == 0);
            fail_push = true;
            xiaotai_tirtc_service_close_wakeup();
            assert(xiaotai_tirtc_busy());
            xiaotai_tirtc_service_close_wakeup();
            xiaotai_tirtc_service_close_wakeup();
            check_cleanup_busy = true; run_worker(); check_cleanup_busy = false;
            assert(notifications == 1 && stopped == 1 && disconnected == 2);
            assert(!xiaotai_tirtc_busy());
            on_conn_accepted((void *)0x3333); run_worker();
            assert(accepted == 1 && atomic_load(&s_connection) == 0x3333);
            assert(stopped == 1); /* Old close cannot stop the new media. */
            in_callback = true; on_disconnected((void *)0x1111); in_callback = false;
            assert(atomic_load(&s_connection) == 0x3333);
        }
    }
    return 0;
}
''')


if __name__ == '__main__':
    unittest.main()
