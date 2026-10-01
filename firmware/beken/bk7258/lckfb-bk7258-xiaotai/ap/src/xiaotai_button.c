#include "xiaotai_button.h"
#include "board_button.h"

#include <common/bk_err.h>
#include "xiaotai_log.h"
#include <os/os.h>
#include <stdbool.h>


#define TAG "xiaotai_button"
#define POLL_MS 20U
#define STABLE_POLLS 3U
#define LONG_PRESS_MS 700U
#define DOUBLE_CLICK_MS 320U

static beken_thread_t s_button_thread;
static xiaotai_button_event_fn s_callback;
static void *s_context;

static void button_task(beken_thread_arg_t argument)
{
    (void)argument;
    bool stable = xiaotai_board_button_released();
    bool candidate = stable;
    unsigned candidate_count = 0;
    uint32_t pressed_at = 0;
    uint32_t released_at = 0;
    bool long_sent = false;
    bool short_pending = false;

    for (;;) {
        uint32_t now = rtos_get_time();
        bool sample = xiaotai_board_button_released();
        if (sample != candidate) {
            candidate = sample;
            candidate_count = 1;
        } else if (candidate_count < STABLE_POLLS) {
            ++candidate_count;
        }
        if (candidate_count == STABLE_POLLS && stable != candidate) {
            stable = candidate;
            if (!stable) {
                pressed_at = now;
                long_sent = false;
                if (s_callback != NULL) {
                    s_callback(XIAOTAI_BUTTON_DOWN, s_context);
                }
            } else {
                if (s_callback != NULL) {
                    s_callback(XIAOTAI_BUTTON_UP, s_context);
                }
                if (!long_sent) {
                    if (short_pending && now - released_at <= DOUBLE_CLICK_MS) {
                        short_pending = false;
                        BK_LOGI(TAG, "KEY double click\n");
                        if (s_callback != NULL) {
                            s_callback(XIAOTAI_BUTTON_DOUBLE, s_context);
                        }
                    } else {
                        short_pending = true;
                        released_at = now;
                    }
                }
            }
        }
        if (!stable && !long_sent && now - pressed_at >= LONG_PRESS_MS) {
            long_sent = true;
            short_pending = false;
            BK_LOGI(TAG, "KEY long press\n");
            if (s_callback != NULL) {
                s_callback(XIAOTAI_BUTTON_LONG, s_context);
            }
        }
        if (stable && short_pending && now - released_at > DOUBLE_CLICK_MS) {
            short_pending = false;
            BK_LOGI(TAG, "KEY short press\n");
            if (s_callback != NULL) {
                s_callback(XIAOTAI_BUTTON_SHORT, s_context);
            }
        }
        rtos_delay_milliseconds(POLL_MS);
    }
}

int xiaotai_button_start(xiaotai_button_event_fn callback, void *context)
{
    if (callback == NULL) return BK_ERR_PARAM;
    if (s_button_thread != NULL) return BK_ERR_BUSY;

    int rc = xiaotai_board_button_init();
    if (rc != BK_OK) return rc;

    s_callback = callback;
    s_context = context;
    rc = rtos_create_thread(&s_button_thread, 4, "xiaotai_button",
                            button_task, 2048, NULL);
    if (rc == BK_OK) {
        BK_LOGI(TAG, "KEY ready on GPIO7: short/double/long/PTT events\n");
    }
    return rc;
}
