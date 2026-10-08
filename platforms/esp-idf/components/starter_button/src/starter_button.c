/*
 * XiaoTai ESP-IDF 产品按键策略。
 *
 * 板 adapter 负责电气配置并把有效电平归一化为 pressed。本任务采用轮询
 * 消抖，避免占用全局 GPIO ISR service。按住只触发一次，并且启动
 * 时若按键已经按下，必须先松开才会响应，避免下载/复位操作误开 AI。
 */
#include "starter_button.h"

#include <stdbool.h>
#include <stdint.h>

#include "esp_heap_caps.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/idf_additions.h"
#include "freertos/task.h"
#include "starter_runtime.h"
#include "xiaotai_board_button.h"

#define BUTTON_POLL_MS 10U
#define BUTTON_DEBOUNCE_MS 50U
#define BUTTON_DOUBLE_CLICK_MS 300U
#define BUTTON_TASK_STACK_BYTES 3072U
#define BUTTON_TASK_PRIORITY 4U

static const char *TAG = "starter_button";
static TaskHandle_t s_button_task;
static const xiaotai_board_button_adapter_t *s_button;

static void post_main_key(bool double_click)
{
    esp_err_t err = starter_runtime_main_key(double_click);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "main key intent not queued: %s", esp_err_to_name(err));
    }
}

static void button_task(void *argument)
{
    (void)argument;
    bool stable_pressed = s_button->read_pressed(s_button->context);
    bool candidate_pressed = stable_pressed;
    bool armed = !stable_pressed;
    uint32_t candidate_ms = 0;
    bool click_pending = false;
    uint32_t click_ms = 0;

    for (;;) {
        vTaskDelay(pdMS_TO_TICKS(BUTTON_POLL_MS));
        if (click_pending) {
            click_ms += BUTTON_POLL_MS;
            if (click_ms >= BUTTON_DOUBLE_CLICK_MS) {
                post_main_key(false);
                click_pending = false;
            }
        }
        bool pressed = s_button->read_pressed(s_button->context);
        if (pressed != candidate_pressed) {
            candidate_pressed = pressed;
            candidate_ms = 0;
            continue;
        }
        if (candidate_ms < BUTTON_DEBOUNCE_MS) {
            candidate_ms += BUTTON_POLL_MS;
            continue;
        }
        if (stable_pressed == candidate_pressed) {
            continue;
        }

        stable_pressed = candidate_pressed;
        if (!stable_pressed) {
            armed = true;
        } else if (armed) {
            armed = false;
            if (click_pending) {
                post_main_key(true);
                click_pending = false;
            } else {
                click_pending = true;
                click_ms = 0;
            }
        }
    }
}

esp_err_t starter_button_start(void)
{
    if (s_button_task != NULL) {
        return ESP_OK;
    }
    s_button = xiaotai_board_button_adapter();
    if (s_button == NULL || s_button->initialize == NULL ||
        s_button->read_pressed == NULL) {
        return ESP_ERR_INVALID_STATE;
    }
    esp_err_t err = (esp_err_t)s_button->initialize(s_button->context);
    if (err != ESP_OK) {
        return err;
    }
    if (xTaskCreateWithCaps(button_task,
                            "ai_button",
                            BUTTON_TASK_STACK_BYTES,
                            NULL,
                            BUTTON_TASK_PRIORITY,
                            &s_button_task,
                            MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT) != pdPASS) {
        s_button_task = NULL;
        return ESP_ERR_NO_MEM;
    }
    ESP_LOGI(TAG,
             "%s ready; single main action, double first-contact/reject",
             s_button->name != NULL ? s_button->name : "user button");
    return ESP_OK;
}
