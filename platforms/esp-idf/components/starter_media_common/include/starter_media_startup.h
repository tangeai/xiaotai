#pragma once
#include <stddef.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

/* Workers cannot touch media resources until the entire initialization commits.
 * On failure only these gated tasks are deleted, never a running media worker. */
typedef struct {
    TaskFunction_t function;
    TaskHandle_t handle;
} starter_media_worker_t;

static inline void starter_media_worker_entry(void *argument)
{
    starter_media_worker_t *worker = argument;
    (void)ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
    worker->function(NULL);
    vTaskDeleteWithCaps(NULL);
}

static inline void starter_media_workers_abort(starter_media_worker_t *workers,
                                                size_t count)
{
    for (size_t i = 0; i < count; ++i) {
        if (workers[i].handle != NULL) {
            vTaskDeleteWithCaps(workers[i].handle);
            workers[i].handle = NULL;
        }
    }
}

static inline void starter_media_workers_commit(starter_media_worker_t *workers,
                                                 size_t count)
{
    for (size_t i = 0; i < count; ++i) xTaskNotifyGive(workers[i].handle);
}
