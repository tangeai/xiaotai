"""Replay a real touch release while the BK control queue is saturated."""
import unittest
from test_s3_product_regressions import ROOT, function, run_c

APP = ROOT / 'firmware/beken/bk7258/lckfb-bk7258-xiaotai/ap/src/xiaotai_app.c'

class BkPttReleaseDelivery(unittest.TestCase):
    def test_release_survives_full_control_queue(self):
        source = APP.read_text()
        callback = function(source, 'touch_event')
        service = function(source, 'service_room_touch_release') if 'static void service_room_touch_release(' in source else 'static void service_room_touch_release(void){}'
        released = function(source, 'room_touch_released') if 'static bool room_touch_released(' in source else ''
        run_c(r'''
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdatomic.h>
#include <stddef.h>
#define BK_OK 0
#define BK_LOGW(...) ((void)0)
#define BK_LOGD(...) ((void)0)
#define CONTROL_TOUCH 1
#define XIAOTAI_TOUCH_DOWN 0
#define XIAOTAI_TOUCH_MOVE 1
#define XIAOTAI_TOUCH_UP 2
#define XIAOTAI_TOUCH_CANCEL 3
#define XIAOTAI_ROOM_ACTION_TALK_STOP 2
typedef unsigned xiaotai_touch_event_t;
typedef struct {unsigned type,generation,x,y,input_event;int error;} control_event_t;
static int s_control_queue,s_room,stops;
static bool full,s_room_touch_talking;
static unsigned s_room_touch_generation;
static atomic_uint s_touch_release_generation;
static control_event_t queued;
static int rtos_push_to_queue(int *q,control_event_t *e,unsigned wait){if(full)return -1;queued=*e;return 0;}
static int xiaotai_room_action(int *room,int action,void *json){assert(action==XIAOTAI_ROOM_ACTION_TALK_STOP);stops++;return 0;}
''' + callback + released + service + r'''
int main(void){
 (void)s_touch_release_generation;(void)s_room;
 /* PTT down already reached the product task; an unrelated event fills its
  * bounded queue while the user lifts their finger. */
 touch_event(XIAOTAI_TOUCH_DOWN,230,210,0);
 s_room_touch_generation=queued.generation;s_room_touch_talking=true;
 full=true;touch_event(XIAOTAI_TOUCH_UP,230,210,0);
 full=false;service_room_touch_release();
 assert(!s_room_touch_talking && stops==1);
 service_room_touch_release();assert(stops==1);
 /* A delayed release from the old gesture must not cancel a new press. */
 touch_event(XIAOTAI_TOUCH_DOWN,230,210,0);
 s_room_touch_generation=queued.generation;s_room_touch_talking=true;
 service_room_touch_release();assert(s_room_touch_talking && stops==1);
 full=true;touch_event(XIAOTAI_TOUCH_UP,230,210,0);
 full=false;service_room_touch_release();assert(!s_room_touch_talking && stops==2);
 /* The same guarantee applies to controller-failure cancellation. */
 touch_event(XIAOTAI_TOUCH_DOWN,230,210,0);
 s_room_touch_generation=queued.generation;s_room_touch_talking=true;
 full=true;touch_event(XIAOTAI_TOUCH_CANCEL,230,210,0);
 full=false;service_room_touch_release();assert(!s_room_touch_talking && stops==3);
}
''')

if __name__ == '__main__':
    unittest.main()
