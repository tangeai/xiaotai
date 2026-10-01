/*
 * Narrow compatibility policy for the prebuilt TiRTC 2.3.0 ESP32-S3 SDK.
 *
 * DWARF and disassembly in the shipped archive show rtc_thread_create() passes
 * 3236 bytes to freertos_ThreadCreateWithStackSize(). The SDK factory already
 * places this stack in PSRAM (MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT), but the
 * full-duplex AI data-channel path exceeds that budget on real hardware.
 * The vendor factory is called internally in one archive, so the link wraps
 * its final external ESP-IDF task-creation call. The exact task name keeps all
 * other application and SDK task budgets unchanged.
 */
#include <string.h>

#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/idf_additions.h"
#include "freertos/task.h"

#define TIRTC_RTC_THREAD_MIN_STACK_BYTES 16384

extern BaseType_t __real_xTaskCreatePinnedToCoreWithCaps(
    TaskFunction_t task_code,
    const char *name,
    configSTACK_DEPTH_TYPE stack_size,
    void *parameters,
    UBaseType_t priority,
    TaskHandle_t *created_task,
    BaseType_t core_id,
    UBaseType_t memory_caps);

BaseType_t __wrap_xTaskCreatePinnedToCoreWithCaps(
    TaskFunction_t task_code,
    const char *name,
    configSTACK_DEPTH_TYPE stack_size,
    void *parameters,
    UBaseType_t priority,
    TaskHandle_t *created_task,
    BaseType_t core_id,
    UBaseType_t memory_caps)
{
    configSTACK_DEPTH_TYPE effective_stack_size = stack_size;
    if (name != NULL && strcmp(name, "rtc_thread") == 0 &&
        effective_stack_size < TIRTC_RTC_THREAD_MIN_STACK_BYTES) {
        effective_stack_size = TIRTC_RTC_THREAD_MIN_STACK_BYTES;
        ESP_LOGI("starter_tirtc",
                 "TiRTC rtc_thread PSRAM stack raised from %u to %u bytes",
                 (unsigned)stack_size,
                 (unsigned)effective_stack_size);
    }
    return __real_xTaskCreatePinnedToCoreWithCaps(task_code,
                                                  name,
                                                  effective_stack_size,
                                                  parameters,
                                                  priority,
                                                  created_task,
                                                  core_id,
                                                  memory_caps);
}
