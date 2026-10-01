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
#define BUTTON_TASK_STACK_BYTES 3072U
#define BUTTON_TASK_PRIORITY 4U

static const char *TAG = "starter_button";
static TaskHandle_t s_button_task;
static const xiaotai_board_button_adapter_t *s_button;

static void toggle_ai(void)
{
    starter_runtime_status_t status = starter_runtime_status();
    esp_err_t err;
    if (status.state == STARTER_RUNTIME_AI_CONNECTING ||
        status.state == STARTER_RUNTIME_AI_ACTIVE) {
        err = starter_runtime_ai_stop();
        if (err == ESP_OK) {
            ESP_LOGI(TAG,
                     "BOOT button queued AI stop from state=%s",
                     starter_runtime_state_name(status.state));
        }
    } else {
        err = starter_runtime_ai_start();
        if (err == ESP_OK) {
            ESP_LOGI(TAG,
                     "BOOT button queued AI start from state=%s",
                     starter_runtime_state_name(status.state));
        }
    }
    if (err != ESP_OK) {
        ESP_LOGW(TAG,
                 "BOOT button request ignored in state=%s: %s",
                 starter_runtime_state_name(status.state),
                 esp_err_to_name(err));
    }
}

static void button_task(void *argument)
{
    (void)argument;
    bool stable_pressed = s_button->read_pressed(s_button->context);
    bool candidate_pressed = stable_pressed;
    bool armed = !stable_pressed;
    uint32_t candidate_ms = 0;

    for (;;) {
        vTaskDelay(pdMS_TO_TICKS(BUTTON_POLL_MS));
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
            toggle_ai();
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
             "%s ready; press once to toggle AI talk",
             s_button->name != NULL ? s_button->name : "user button");
    return ESP_OK;
}
