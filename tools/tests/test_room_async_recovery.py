"""Fault injection through the room controller and MQTT adapter interfaces."""
from pathlib import Path
import subprocess
import tempfile
import unittest
from test_s3_product_regressions import function

ROOT = Path(__file__).resolve().parents[2]


def replay(body, signal=False):
    with tempfile.TemporaryDirectory() as directory:
        source = Path(directory) / 'test.c'
        source.write_text('#define main existing_suite\n#include "' +
                          str(ROOT / 'product/tests/test_room_resilience.c') +
                          '"\n#undef main\n' + body)
        executable = Path(directory) / 'test'
        subprocess.run(['cc', '-std=c11', '-Wall', '-Werror',
                        '-I' + str(ROOT / 'product/include'), '-I/usr/include/cjson',
                        str(source), str(ROOT / 'product/src/xiaotai_room.c'),
                        *([str(ROOT / 'product/src/xiaotai_signal.c')] if signal else []),
                        '-lcjson', '-o', str(executable)], check=True, capture_output=True)
        result = subprocess.run([str(executable)], capture_output=True, text=True)
        assert result.returncode == 0, result.stderr


class RoomAsyncRecovery(unittest.TestCase):
    def test_lost_assignment_and_leave_results_expire_without_polling(self):
        replay(r'''
int main(void) {
    fixture_t f={0}; xiaotai_room_t room=active_room(&f);
    room.joined=false; room.runtime_generation=0;
    xiaotai_room_action(&room,XIAOTAI_ROOM_ACTION_SYNC,NULL);
    xiaotai_room_tick(&room); assert(room.request_pending);
    f.now_ms=60000; xiaotai_room_tick(&room);
    assert(!room.request_pending && f.assignment_requests==1);
    assert(xiaotai_room_action(&room,XIAOTAI_ROOM_ACTION_LEAVE,NULL)==0);
    f.now_ms+=60000; xiaotai_room_tick(&room);
    assert(!room.request_pending && !room.leave_pending);
    assert(f.assignment_requests==1);
}
''')

    def test_adapter_allocation_and_queue_failure_recover_on_deadline(self):
        src=(ROOT/'platforms/beken/runtime/src/xiaotai_app.c').read_text()
        replay(r'''
#include <stdlib.h>
#define BK_OK 0
#define BK_LOGW(...) ((void)0)
typedef enum { CONTROL_ROOM_ASSIGNMENT, CONTROL_ROOM_TOKEN, CONTROL_ROOM_PRESENCE } control_event_type_t;
typedef struct { control_event_type_t type; uint32_t generation; char *text; } control_event_t;
static void *s_control_queue=(void*)1;
static bool fail_alloc;
static unsigned live_allocations;
static void *os_malloc(size_t size) {
    if (fail_alloc) return NULL;
    void *p=malloc(size); if(p) live_allocations++; return p;
}
static void os_free(void *p) {if(p) live_allocations--; free(p);}
static int rtos_push_to_queue(void **queue,control_event_t *event,int timeout) {
    (void)queue; (void)event; (void)timeout; return -1;
}
''' + function(src,'queue_text_event') + function(src,'room_post_response') + r'''
int main(void) {
    for(unsigned mode=0;mode<2;mode++) {
        fixture_t f={0}; xiaotai_room_t room=active_room(&f);
        room.joined=false; room.runtime_generation=0;
        room.port.post_response=room_post_response;
        fail_alloc=mode==0;
        xiaotai_room_action(&room,XIAOTAI_ROOM_ACTION_SYNC,NULL);
        xiaotai_room_tick(&room);
        f.callback("{\"code\":-1}",f.callback_ticket,f.callback_context);
        assert(room.request_pending && live_allocations==0);
        f.now_ms=30000; xiaotai_room_tick(&room);
        assert(!room.request_pending && f.assignment_requests==1);
        assert(xiaotai_room_action(&room,XIAOTAI_ROOM_ACTION_LEAVE,NULL)==0);
    }
}
''')

    def test_pending_http_cannot_extend_lease(self):
        replay(r'''
int main(void) {
    fixture_t f={.now_ms=1000}; xiaotai_room_t room=active_room(&f);
    room.lease_deadline_ms=1000; room.request_pending=true;
    xiaotai_room_tick(&room); xiaotai_room_tick(&room);
    assert(f.transport_disconnects==1);
}
''')

    def test_join_timeout_disconnects_once(self):
        replay(r'''
int main(void) {
    fixture_t f={.now_ms=1000}; xiaotai_room_t room=active_room(&f);
    room.joined=false; room.connecting=true;
    xiaotai_room_handle_connected(&room,room.generation);
    f.now_ms=61000; xiaotai_room_tick(&room); xiaotai_room_tick(&room);
    assert(f.transport_disconnects==1);
    xiaotai_room_handle_disconnected(&room,room.generation,-1);
    assert(!room.connecting && f.media_releases==1);
}
''')

    def test_duplicate_and_late_join_do_not_start_media(self):
        replay(r'''
int main(void) {
    fixture_t f={0}; xiaotai_room_t room=active_room(&f);
    const char *join="{\"id\":1,\"result\":{\"session_id\":\"s\","
        "\"input_audio\":{\"codec\":\"g711a\",\"sample_rate\":8000,\"channels\":1},"
        "\"output_audio\":{\"codec\":\"g711a\",\"sample_rate\":8000,\"channels\":1}}}";
    xiaotai_room_handle_command(&room,room.generation,join);
    assert(f.media_starts==0 && f.transport_disconnects==0);
    room.joined=false; room.connecting=true; room.foreground=false;
    xiaotai_room_handle_command(&room,room.generation,join);
    assert(f.media_starts==0);
}
''')

    def test_closed_notification_matches_room_and_version(self):
        src=(ROOT/'platforms/beken/runtime/src/xiaotai_app.c').read_text()
        start=src.index('} else if (signal->type == XIAOTAI_SIGNAL_ROOM_CLOSED) {')
        end=src.index('} else if (signal->type == XIAOTAI_SIGNAL_CALL_ENDED)',start)
        branch=src[start+7:end]+'}'
        replay(r'''
#include "xiaotai_signal.h"
#define BK_LOGI(...) ((void)0)
static xiaotai_room_t s_room;
static void visit(const xiaotai_signal_view_t *signal,void *context) {
    (void)context;
''' + branch + r'''
}
int main(void) {
    fixture_t f={0}; s_room=active_room(&f); s_room.assignment_version=20;
    const char *ignored[]={
      "{\"type\":\"room_closed\",\"room_id\":\"other\",\"assignment_version\":21}",
      "{\"type\":\"room_closed\",\"room_id\":\"room-1\",\"assignment_version\":19}",
      "{\"type\":\"room_closed\",\"room_id\":\"room-1\"}",
      "{\"type\":\"room_closed\",\"room_id\":\"room-1\",\"assignment_version\":20.5}"};
    for(unsigned i=0;i<sizeof(ignored)/sizeof(ignored[0]);i++) {
        xiaotai_signal_decode(ignored[i],visit,NULL);
        assert(s_room.assigned && f.transport_disconnects==0);
    }
    const char *closed="{\"type\":\"room_closed\",\"room_id\":\"room-1\",\"assignment_version\":20}";
    xiaotai_signal_decode(closed,visit,NULL);
    xiaotai_signal_decode(closed,visit,NULL);
    assert(!s_room.assigned && f.transport_disconnects==1);
}
''', signal=True)

if __name__ == '__main__':
    unittest.main()
