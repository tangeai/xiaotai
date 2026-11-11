#include "starter_audio_trace.h"
#include <stdatomic.h>
#include <stdlib.h>
#ifdef ESP_PLATFORM
#include "esp_heap_caps.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#endif
#define TRACE_CAPACITY 4096U
static atomic_flag trace_lock = ATOMIC_FLAG_INIT;
static atomic_bool trace_armed;
static atomic_uint trace_epoch, trace_lost;
static starter_audio_trace_row_t *trace_rows;
static size_t trace_count;
static void lock_wait(void)
{
    while (atomic_flag_test_and_set_explicit(&trace_lock, memory_order_acquire)) {
#ifdef ESP_PLATFORM
        vTaskDelay(1);
#endif
    }
}
static void unlock(void)
{
    atomic_flag_clear_explicit(&trace_lock, memory_order_release);
}
bool starter_audio_trace_arm(void)
{
    /* Console only; caller first makes the media session idle. */
    lock_wait();
    if (atomic_load(&trace_armed)) { unlock(); return false; }
    if (!trace_rows) {
#ifdef ESP_PLATFORM
        trace_rows = heap_caps_calloc(TRACE_CAPACITY, sizeof(*trace_rows),
                                     MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
#else
        trace_rows = calloc(TRACE_CAPACITY, sizeof(*trace_rows));
#endif
    }
    bool ok = trace_rows != NULL;
    if (ok) {
        trace_count = 0;
        atomic_store(&trace_lost, 0);
        atomic_fetch_add(&trace_epoch, 1);
        atomic_store(&trace_armed, true);
    }
    unlock();
    return ok;
}
void starter_audio_trace_stop(void)
{
    lock_wait(); atomic_store(&trace_armed, false); unlock();
}
void starter_audio_trace_clear(void)
{
    /* Serialize against record/read; epoch invalidates an in-flight old writer. */
    lock_wait();
    atomic_store(&trace_armed, false);
    atomic_fetch_add(&trace_epoch, 1);
    free(trace_rows);
    trace_rows = NULL;
    trace_count = 0;
    atomic_store(&trace_lost, 0);
    unlock();
}
void starter_audio_trace_status(size_t *count, uint32_t *lost, bool *armed)
{
    lock_wait();
    *count = trace_count; *lost = atomic_load(&trace_lost);
    *armed = atomic_load(&trace_armed);
    unlock();
}
bool starter_audio_trace_read(size_t index, starter_audio_trace_row_t *row)
{
    lock_wait();
    bool ok = row && !atomic_load(&trace_armed) && index < trace_count;
    if (ok) *row = trace_rows[index];
    unlock();
    return ok;
}
void starter_audio_trace_record(unsigned stage, uint32_t generation,
    uint32_t timestamp_ms, uint32_t length, unsigned media, unsigned flags,
    unsigned stream_id, const void *data, uint32_t at_ms, uint32_t detail)
{
    if (!atomic_load(&trace_armed)) return;
    uint32_t epoch = atomic_load(&trace_epoch);
    if (atomic_flag_test_and_set_explicit(&trace_lock, memory_order_acquire)) {
        atomic_fetch_add(&trace_lost, 1); return;
    }
    if (!atomic_load(&trace_armed) || epoch != atomic_load(&trace_epoch)) {
        unlock(); return;
    }
    if (trace_count == TRACE_CAPACITY) {
        atomic_fetch_add(&trace_lost, 1); unlock(); return;
    }
    /* Bounded fingerprint. Oversize/null input is explicitly marked rather
     * than dereferencing beyond the supported callback payload contract. */
    uint32_t hash = 2166136261U;
    if (!data || length > 1500U) hash = 0;
    else for (size_t i = 0; i < length; ++i)
        hash = (hash ^ ((const uint8_t *)data)[i]) * 16777619U;
    trace_rows[trace_count++] = (starter_audio_trace_row_t) {
        .stage=stage, .generation=generation, .timestamp_ms=timestamp_ms,
        .length=length, .media=media, .flags=flags, .stream_id=stream_id,
        .hash=hash, .at_ms=at_ms, .detail=detail,
    };
    unlock();
}
