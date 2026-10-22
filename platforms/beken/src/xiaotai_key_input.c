#include "xiaotai_button.h"
#include "board_button.h"
#include "xiaotai_key_gesture.h"
#include <os/os.h>
static beken_thread_t thread;
static xiaotai_button_event_fn callback;
static void *context;
static void key_task(beken_thread_arg_t arg)
{
    (void)arg;
    xiaotai_key_gesture_t keys[3] = {0};
    for (;;) {
        uint32_t now = rtos_get_time();
        for (unsigned i=0; i<3; ++i) {
            xiaotai_key_event_t e = xiaotai_key_sample(&keys[i], xiaotai_board_key_released(i), now);
            if (i==0 && e==XIAOTAI_KEY_SHORT) callback(XIAOTAI_BUTTON_SHORT, context);
            else if (i==0 && e==XIAOTAI_KEY_DOUBLE) callback(XIAOTAI_BUTTON_DOUBLE, context);
            else if (i==1 && e==XIAOTAI_KEY_SHORT) callback(XIAOTAI_BUTTON_VOLUME_UP, context);
            else if (i==2 && e==XIAOTAI_KEY_SHORT) callback(XIAOTAI_BUTTON_VOLUME_DOWN, context);
            else if (i==1 && e==XIAOTAI_KEY_LONG) callback(XIAOTAI_BUTTON_PROVISION, context);
        }
        rtos_delay_milliseconds(20);
    }
}
int xiaotai_button_start(xiaotai_button_event_fn cb, void *ctx)
{
    if (!cb) return BK_ERR_PARAM;
    if (thread) return BK_ERR_BUSY;
    int rc=xiaotai_board_button_init();
    if (rc!=BK_OK) return rc;
    callback=cb; context=ctx;
    return rtos_create_thread(&thread,4,"xiaotai_keys",key_task,2048, NULL);
}
