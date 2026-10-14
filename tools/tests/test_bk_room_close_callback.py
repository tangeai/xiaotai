"""The TiRTC close callback must finish without serial logging in room mode."""

import unittest

from test_s3_product_regressions import ROOT, function, run_c


class BKRoomCloseCallback(unittest.TestCase):
    def test_room_close_callback_clears_owner_and_queues_worker_event(self):
        source = (ROOT / 'firmware/beken/bk7258/lckfb-bk7258-xiaotai/ap/src/xiaotai_tirtc.c').read_text()
        run_c(r'''
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdatomic.h>
#include <string.h>
#define BK_OK 0
#define BK_LOGI(...) assert(!"serial logging in TiRTC close callback")
#define BK_LOGE(...) assert(!"serial logging in TiRTC close callback")
typedef void *tirtc_conn_t;
typedef enum { CONNECTION_NONE=0, CONNECTION_CALL=3, CONNECTION_VOIP=4, CONNECTION_ROOM=5 } connection_mode_t;
typedef enum { MEDIA_EVENT_CLOSED=3 } media_event_type_t;
typedef struct { media_event_type_t type; tirtc_conn_t connection; uint32_t generation;
                 int error; connection_mode_t mode; } media_event_t;
static void *s_media_queue;
static atomic_uintptr_t s_connection, s_outgoing_cleanup_connection;
static atomic_uint_fast32_t s_active_generation;
static atomic_int s_connection_error, s_connection_mode;
static atomic_bool s_active_disconnect_queued, s_audio_subscribed, s_video_subscribed;
static atomic_bool s_call_is_caller, s_call_accept_seen, s_voip_accept_seen;
static char s_active_call_room[128];
static media_event_t queued;
static int queue_count;
static void release_outgoing_connect(void) { assert(!"unexpected cleanup"); }
static unsigned rtos_get_free_heap_size(void) { assert(!"heap query in callback"); return 0; }
static unsigned rtos_get_psram_free_heap_size(void) { assert(!"heap query in callback"); return 0; }
static void require_recovery_if_heap_low(const char *reason) { (void)reason; assert(!"recovery in callback"); }
static int rtos_push_to_queue(void **queue, const media_event_t *event, unsigned timeout) {
    assert(queue == &s_media_queue && timeout == 0);
    queued = *event;
    queue_count++;
    return BK_OK;
}
''' + function(source, 'on_disconnected') + r'''
int main(void) {
    tirtc_conn_t handle = (void *)0x1234;
    atomic_store(&s_connection, (uintptr_t)handle);
    atomic_store(&s_active_generation, 7);
    atomic_store(&s_connection_error, 0);
    atomic_store(&s_connection_mode, CONNECTION_ROOM);
    atomic_store(&s_active_disconnect_queued, true);
    atomic_store(&s_audio_subscribed, true);
    on_disconnected(handle);
    assert(queue_count == 1 && queued.type == MEDIA_EVENT_CLOSED);
    assert(queued.mode == CONNECTION_ROOM && queued.generation == 7 && queued.error == 0);
    assert(atomic_load(&s_connection) == 0 && atomic_load(&s_active_generation) == 0);
    assert(!atomic_load(&s_active_disconnect_queued));
    assert(!atomic_load(&s_audio_subscribed));
    return 0;
}
''')


if __name__ == '__main__':
    unittest.main()
