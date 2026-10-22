#include "xiaotai_touch.h"
#include "xiaotai_touch_policy.h"
#include "board_touch.h"

#include <common/bk_err.h>
#include "xiaotai_log.h"
#include <os/os.h>
#include <stdbool.h>

#define TAG "xiaotai_touch"
#define TOUCH_POLL_MS 15U

static beken_thread_t s_touch_thread;
static xiaotai_touch_event_fn s_callback;
static void *s_context;

static void dispatch_point(const xiaotai_board_touch_point_t *point,
                           xiaotai_touch_policy_t *policy)
{
    xiaotai_touch_policy_result_t result =
        xiaotai_touch_policy_apply(policy, point);
    if (result.emit && s_callback != NULL) {
        s_callback(result.event, result.x, result.y, s_context);
    }
}

static void touch_task(beken_thread_arg_t argument)
{
    (void)argument;
    xiaotai_touch_policy_t policy = {0};
    for (;;) {
        xiaotai_board_touch_point_t point = {0};
        int rc = xiaotai_board_touch_read(&point);
        if (rc == BK_OK) {
            dispatch_point(&point, &policy);
            if (point.more) continue;
        }
        rtos_delay_milliseconds(TOUCH_POLL_MS);
    }
}

int xiaotai_touch_start(xiaotai_touch_event_fn callback, void *context)
{
    if (callback == NULL) return BK_ERR_PARAM;
    if (s_touch_thread != NULL) return BK_ERR_BUSY;

    int rc = xiaotai_board_touch_open();
    if (rc != BK_OK) return rc;

    s_callback = callback;
    s_context = context;
    rc = rtos_create_thread(&s_touch_thread, 4, "xiaotai_touch",
                            touch_task, 2048, NULL);
    if (rc != BK_OK) {
        s_callback = NULL;
        s_context = NULL;
        xiaotai_board_touch_close();
        return rc;
    }

    return BK_OK;
}
