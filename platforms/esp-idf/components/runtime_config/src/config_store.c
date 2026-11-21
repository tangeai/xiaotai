#include "config_store.h"
#include <stdatomic.h>
#include <stdint.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static atomic_uint s_writer;
static atomic_bool s_reset;
static atomic_uintptr_t s_reset_owner;

bool config_store_write_begin(void)
{
    if (atomic_load(&s_reset))
        return atomic_load(&s_reset_owner) == (uintptr_t)xTaskGetCurrentTaskHandle();
    unsigned expected = 0;
    if (!atomic_compare_exchange_strong(&s_writer, &expected, 1)) return false;
    if (atomic_load(&s_reset)) {
        atomic_store(&s_writer, 0);
        return false;
    }
    return true;
}

void config_store_write_end(void)
{
    if (atomic_load(&s_reset_owner) != (uintptr_t)xTaskGetCurrentTaskHandle())
        atomic_store(&s_writer, 0);
}

bool config_store_reset_begin(void)
{
    bool expected = false;
    if (!atomic_compare_exchange_strong(&s_reset, &expected, true)) return false;
    if (atomic_load(&s_writer) != 0) {
        atomic_store(&s_reset, false);
        return false;
    }
    atomic_store(&s_reset_owner, (uintptr_t)xTaskGetCurrentTaskHandle());
    return true;
}

void config_store_reset_end(bool success)
{
    atomic_store(&s_reset_owner, 0);
    if (!success) atomic_store(&s_reset, false);
}
