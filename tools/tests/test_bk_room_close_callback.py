"""The TiRTC room close callback must avoid RTOS queues and serial logging."""

import unittest

from test_s3_product_regressions import ROOT, function, run_c


class BKRoomCloseCallback(unittest.TestCase):
    def test_control_task_services_close_even_when_events_keep_arriving(self):
        source = (ROOT / 'firmware/beken/bk7258/lckfb-bk7258-xiaotai/ap/src/xiaotai_app.c').read_text()
        control = function(source, 'control_task')
        self.assertLess(control.index('xiaotai_tirtc_service_close_wakeup();'),
                        control.index('rtos_pop_from_queue(&s_control_queue'))

    def test_idle_worker_blocks_and_control_wakes_close(self):
        source = (ROOT / 'firmware/beken/bk7258/lckfb-bk7258-xiaotai/ap/src/xiaotai_tirtc.c').read_text()
        run_c(r'''
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdatomic.h>
#include <stddef.h>
#define BK_OK 0
#define BK_FAIL -1
#define BEKEN_WAIT_FOREVER 0xffffffffU
enum { MEDIA_EVENT_WAKE = 15, MEDIA_EVENT_CLOSED = 3 };
typedef struct { int type; unsigned generation; } media_event_t;
typedef void *beken_queue_t;
static beken_queue_t s_media_queue = (void *)1;
static atomic_int s_pending_close_state;
static media_event_t s_pending_close_event;
static media_event_t queued_event;
static unsigned queue_waits, queue_pushes;
static bool fail_next_push;
static int rtos_pop_from_queue(beken_queue_t *queue, media_event_t *event,
                               unsigned timeout_ms) {
    assert(queue == &s_media_queue && timeout_ms == BEKEN_WAIT_FOREVER);
    queue_waits++;
    if (!queued_event.type) return BK_FAIL;
    *event = queued_event;
    queued_event.type = 0;
    return BK_OK;
}
static int rtos_push_to_queue(beken_queue_t *queue, const media_event_t *event,
                              unsigned timeout_ms) {
    assert(queue == &s_media_queue && timeout_ms == 0);
    queued_event = *event;
    queue_pushes++;
    if (fail_next_push) {
        fail_next_push = false;
        queued_event.type = 0;
        return BK_FAIL;
    }
    return BK_OK;
}
''' + function(source, 'take_pending_close_event') +
              function(source, 'media_wait_next_event') +
              function(source, 'xiaotai_tirtc_service_close_wakeup') + r'''
int main(void) {
    media_event_t event = {0};
    assert(!media_wait_next_event(&event));
    assert(queue_waits == 1 && queue_pushes == 0);
    s_pending_close_event = (media_event_t){.type=MEDIA_EVENT_CLOSED, .generation=7};
    atomic_store(&s_pending_close_state, 2);
    xiaotai_tirtc_service_close_wakeup();
    xiaotai_tirtc_service_close_wakeup();
    assert(queue_pushes == 1 && atomic_load(&s_pending_close_state) == 3);
    assert(media_wait_next_event(&event));
    assert(event.type == MEDIA_EVENT_CLOSED && event.generation == 7);
    assert(atomic_load(&s_pending_close_state) == 0);
    s_pending_close_event = (media_event_t){.type=MEDIA_EVENT_CLOSED, .generation=8};
    atomic_store(&s_pending_close_state, 2);
    fail_next_push = true;
    xiaotai_tirtc_service_close_wakeup();
    assert(atomic_load(&s_pending_close_state) == 2);
    xiaotai_tirtc_service_close_wakeup();
    assert(media_wait_next_event(&event));
    assert(event.generation == 8 && atomic_load(&s_pending_close_state) == 0);
    return 0;
}
''')

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
static atomic_uintptr_t s_connection, s_outgoing_cleanup_connection;
static atomic_uint_fast32_t s_active_generation;
static atomic_int s_connection_error, s_connection_mode;
static atomic_bool s_active_disconnect_queued, s_audio_subscribed, s_video_subscribed;
static atomic_bool s_call_is_caller, s_call_accept_seen, s_voip_accept_seen;
static atomic_bool s_recovery_required;
static atomic_int s_pending_close_state;
static media_event_t s_pending_close_event;
static char s_active_call_room[128];
static void release_outgoing_connect(void) { assert(!"unexpected cleanup"); }
static unsigned rtos_get_free_heap_size(void) { assert(!"heap query in callback"); return 0; }
static unsigned rtos_get_psram_free_heap_size(void) { assert(!"heap query in callback"); return 0; }
static void require_recovery_if_heap_low(const char *reason) { (void)reason; assert(!"recovery in callback"); }
static int rtos_push_to_queue(void **queue, const media_event_t *event, unsigned timeout) {
    (void)queue; (void)event; (void)timeout;
    assert(!"RTOS queue in TiRTC close callback");
    return BK_OK;
}
''' + function(source, 'take_pending_close_event') +
              function(source, 'on_disconnected') + r'''
int main(void) {
    tirtc_conn_t handle = (void *)0x1234;
    atomic_store(&s_connection, (uintptr_t)handle);
    atomic_store(&s_active_generation, 7);
    atomic_store(&s_connection_error, 0);
    atomic_store(&s_connection_mode, CONNECTION_ROOM);
    atomic_store(&s_active_disconnect_queued, true);
    atomic_store(&s_audio_subscribed, true);
    on_disconnected(handle);
    assert(atomic_load(&s_pending_close_state) == 2);
    assert(s_pending_close_event.type == MEDIA_EVENT_CLOSED);
    assert(s_pending_close_event.connection == handle);
    assert(s_pending_close_event.mode == CONNECTION_ROOM &&
           s_pending_close_event.generation == 7 && s_pending_close_event.error == 0);
    assert(atomic_load(&s_connection) == 0 && atomic_load(&s_active_generation) == 0);
    assert(!atomic_load(&s_active_disconnect_queued));
    assert(!atomic_load(&s_audio_subscribed));
    assert(!atomic_load(&s_recovery_required));
    media_event_t delivered = {0};
    atomic_store(&s_pending_close_state, 3);
    assert(take_pending_close_event(&delivered));
    assert(atomic_load(&s_pending_close_state) == 0);
    assert(delivered.connection == handle && delivered.mode == CONNECTION_ROOM);
    assert(delivered.generation == 7 && delivered.error == 0);
    assert(!take_pending_close_event(&delivered));
    return 0;
}
''')


if __name__ == '__main__':
    unittest.main()
