#include "xiaotai_metrics.h"

#include "xiaotai_log.h"
#include <lwip/stats.h>
#include <modules/wifi.h>
#include <os/mem.h>
#include <os/os.h>
#include <stdatomic.h>
#include <stdio.h>
#include <string.h>

#include "FreeRTOS.h"
#include "task.h"

#define TAG "xiaotai_metrics"
#define METRICS_INTERVAL_MS 10000U
#define METRICS_TASK_MAX 64U

static beken_thread_t s_thread;
static beken_mutex_t s_state_mutex;
static bool s_state_mutex_ready;
static char s_state[24] = "BOOT";
static atomic_uint s_sta_connects;
static atomic_uint s_sta_disconnects;
static uint32_t s_previous_total_runtime;
static uint32_t s_previous_idle_runtime;
#if LWIP_STATS && LINK_STATS
static uint32_t s_previous_rx_packets;
static uint32_t s_previous_tx_packets;
#endif

void xiaotai_metrics_set_state(const char *state)
{
    if (!s_state_mutex_ready || state == NULL) return;
    rtos_lock_mutex(&s_state_mutex);
    snprintf(s_state, sizeof(s_state), "%s", state);
    rtos_unlock_mutex(&s_state_mutex);
}

void xiaotai_metrics_network_event(bool connected)
{
    atomic_fetch_add_explicit(connected ? &s_sta_connects : &s_sta_disconnects,
                              1U, memory_order_relaxed);
}

static int cpu_and_stack_snapshot(unsigned *cpu_percent,
                                  unsigned *stack_low_words,
                                  unsigned *task_count,
                                  char *stack_low_task,
                                  size_t stack_low_task_capacity)
{
    UBaseType_t count = uxTaskGetNumberOfTasks();
    if (count == 0U || count > METRICS_TASK_MAX) return BK_FAIL;
    TaskStatus_t *tasks = psram_malloc(count * sizeof(*tasks));
    if (tasks == NULL) return BK_ERR_NO_MEM;
    uint32_t total_runtime = 0U;
    count = uxTaskGetSystemState(tasks, count, &total_runtime);
    uint32_t idle_runtime = 0U;
    configSTACK_DEPTH_TYPE minimum_stack = (configSTACK_DEPTH_TYPE)-1;
    for (UBaseType_t i = 0; i < count; ++i) {
        if (strncmp(tasks[i].pcTaskName, "IDLE", 4U) == 0) {
            idle_runtime += tasks[i].ulRunTimeCounter;
        }
        if (tasks[i].usStackHighWaterMark < minimum_stack) {
            minimum_stack = tasks[i].usStackHighWaterMark;
            if (stack_low_task != NULL && stack_low_task_capacity > 0U) {
                snprintf(stack_low_task, stack_low_task_capacity, "%s",
                         tasks[i].pcTaskName);
            }
        }
    }
    uint32_t total_delta = total_runtime - s_previous_total_runtime;
    uint32_t idle_delta = idle_runtime - s_previous_idle_runtime;
    unsigned busy = 0U;
    if (s_previous_total_runtime != 0U && total_delta != 0U) {
        /* uxTaskGetSystemState() returns one wall-clock runtime counter while
         * the idle counters accumulate independently on both BK7258 cores. */
#ifdef CONFIG_CPU_CNT
        const uint64_t available = (uint64_t)total_delta * CONFIG_CPU_CNT;
#else
        const uint64_t available = total_delta;
#endif
        if ((uint64_t)idle_delta > available) idle_delta = (uint32_t)available;
        busy = (unsigned)(((available - idle_delta) * 100U) / available);
    }
    s_previous_total_runtime = total_runtime;
    s_previous_idle_runtime = idle_runtime;
    *cpu_percent = busy;
    *stack_low_words = minimum_stack == (configSTACK_DEPTH_TYPE)-1 ? 0U :
                       (unsigned)minimum_stack;
    *task_count = (unsigned)count;
    psram_free(tasks);
    return BK_OK;
}

static void emit_snapshot(void)
{
    char state[sizeof(s_state)];
    rtos_lock_mutex(&s_state_mutex);
    snprintf(state, sizeof(state), "%s", s_state);
    rtos_unlock_mutex(&s_state_mutex);
    wifi_link_status_t link = {0};
    int rssi = bk_wifi_sta_get_link_status(&link) == BK_OK ? link.rssi : -128;
    unsigned cpu = 0U;
    unsigned stack_low = 0U;
    unsigned tasks = 0U;
    char stack_low_task[configMAX_TASK_NAME_LEN + 1U] = "-";
    int cpu_rc = cpu_and_stack_snapshot(&cpu, &stack_low, &tasks,
                                        stack_low_task,
                                        sizeof(stack_low_task));
#if LWIP_STATS && LINK_STATS
    uint32_t rx_packets = lwip_stats.link.recv;
    uint32_t tx_packets = lwip_stats.link.xmit;
    uint32_t rx_pps = (rx_packets - s_previous_rx_packets) * 1000U /
                      METRICS_INTERVAL_MS;
    uint32_t tx_pps = (tx_packets - s_previous_tx_packets) * 1000U /
                      METRICS_INTERVAL_MS;
    s_previous_rx_packets = rx_packets;
    s_previous_tx_packets = tx_packets;
#else
    uint32_t rx_packets = 0U;
    uint32_t tx_packets = 0U;
    uint32_t rx_pps = 0U;
    uint32_t tx_pps = 0U;
#endif
    BK_LOGI(TAG,
            "METRICS {\"uptime_ms\":%u,\"state\":\"%s\","
            "\"internal\":{\"free\":%u,\"minimum\":%u,"
            "\"largest\":null,\"blocks\":null},"
            "\"psram\":{\"free\":%u,\"minimum\":%u,"
            "\"largest\":null},"
            "\"cpu_percent\":%d,\"tasks\":%u,\"stack_low_words\":%u,"
            "\"stack_low_task\":\"%s\","
            "\"network\":{\"rssi\":%d,\"rx_packets\":%u,"
            "\"tx_packets\":%u,\"rx_pps\":%u,\"tx_pps\":%u,"
            "\"connects\":%u,\"disconnects\":%u}}\n",
            (unsigned)rtos_get_time(), state,
            (unsigned)rtos_get_free_heap_size(),
            (unsigned)rtos_get_minimum_free_heap_size(),
            (unsigned)rtos_get_psram_free_heap_size(),
            (unsigned)rtos_get_psram_minimum_free_heap_size(),
            cpu_rc == BK_OK ? (int)cpu : -1, tasks, stack_low,
            stack_low_task, rssi,
            (unsigned)rx_packets, (unsigned)tx_packets,
            (unsigned)rx_pps, (unsigned)tx_pps,
            atomic_load_explicit(&s_sta_connects, memory_order_relaxed),
            atomic_load_explicit(&s_sta_disconnects, memory_order_relaxed));
}

static void metrics_task(beken_thread_arg_t argument)
{
    (void)argument;
    for (;;) {
        emit_snapshot();
        rtos_delay_milliseconds(METRICS_INTERVAL_MS);
    }
}

int xiaotai_metrics_start(void)
{
    if (s_thread != NULL) return BK_ERR_BUSY;
    if (!s_state_mutex_ready) {
        int rc = rtos_init_mutex(&s_state_mutex);
        if (rc != BK_OK) return rc;
        s_state_mutex_ready = true;
    }
    return rtos_create_psram_thread(&s_thread, 2, "xiaotai_metrics",
                                    metrics_task, 4096, NULL);
}
