#include "xiaotai_metrics.h"

#include "xiaotai_log.h"
#include <components/system.h>
#include <lwip/stats.h>
#include <os/mem.h>
#include <os/os.h>
#include <stdatomic.h>
#include <stdio.h>
#include <string.h>

#include "FreeRTOS.h"
#include "task.h"

#define TAG "xiaotai_metrics"
#define METRICS_INTERVAL_MS 10000U

static beken_thread_t s_thread;
static beken_mutex_t s_state_mutex;
static bool s_state_mutex_ready;
static char s_state[24] = "BOOT";
static xiaotai_metrics_snapshot_t s_snapshot;
static struct {uint32_t at_ms; char state[24];} s_events[7];
static unsigned s_event_count, s_event_next;

void xiaotai_metrics_snapshot(xiaotai_metrics_snapshot_t *out)
{
    if (out == NULL) return;
    memset(out, 0, sizeof(*out));
    if (!s_state_mutex_ready) return;
    rtos_lock_mutex(&s_state_mutex);
    *out = s_snapshot;
    snprintf(out->state, sizeof(out->state), "%s", s_state);
    rtos_unlock_mutex(&s_state_mutex);
}

void xiaotai_metrics_copy_events(char *out, size_t capacity)
{
    if (out == NULL || capacity == 0U) return;
    out[0] = '\0';
    if (!s_state_mutex_ready) return;
    rtos_lock_mutex(&s_state_mutex);
    size_t used = 0U;
    for (unsigned i = 0; i < s_event_count; ++i) {
        unsigned index = (s_event_next + 7U - 1U - i) % 7U;
        int n = snprintf(out + used, capacity - used, "%u s  %s\n",
                         (unsigned)(s_events[index].at_ms / 1000U), s_events[index].state);
        if (n < 0 || (size_t)n >= capacity - used) break;
        used += (size_t)n;
    }
    if (s_event_count == 0U) snprintf(out, capacity, "暂无事件");
    rtos_unlock_mutex(&s_state_mutex);
}
static atomic_uint s_sta_connects;
static atomic_uint s_sta_disconnects;
static atomic_int s_rssi = ATOMIC_VAR_INIT(-128);

int xiaotai_metrics_cached_rssi(void)
{
    return atomic_load_explicit(&s_rssi, memory_order_relaxed);
}
#if LWIP_STATS && LINK_STATS
static uint32_t s_previous_rx_packets;
static uint32_t s_previous_tx_packets;
#endif

void xiaotai_metrics_set_state(const char *state)
{
    if (!s_state_mutex_ready || state == NULL) return;
    rtos_lock_mutex(&s_state_mutex);
    if (strncmp(s_state, state, sizeof(s_state) - 1U) != 0) {
        s_events[s_event_next].at_ms = rtos_get_time();
        snprintf(s_events[s_event_next].state, sizeof(s_events[s_event_next].state), "%s", state);
        s_event_next = (s_event_next + 1U) % 7U;
        if (s_event_count < 7U) ++s_event_count;
    }
    snprintf(s_state, sizeof(s_state), "%s", state);
    rtos_unlock_mutex(&s_state_mutex);
}

void xiaotai_metrics_network_event(bool connected)
{
    if (!connected) atomic_store_explicit(&s_rssi, -128, memory_order_relaxed);
    atomic_fetch_add_explicit(connected ? &s_sta_connects : &s_sta_disconnects,
                              1U, memory_order_relaxed);
}

static void emit_snapshot(void)
{
    bk_printf_raw_nonblock(BK_LOG_NONE, NULL, "HBTRACE metrics-begin\n");
    char state[sizeof(s_state)];
    rtos_lock_mutex(&s_state_mutex);
    snprintf(state, sizeof(state), "%s", s_state);
    rtos_unlock_mutex(&s_state_mutex);
    /* This task runs unattended. The Wi-Fi link-status API performs a
     * synchronous AP-to-CP RPC. Until a safe asynchronous source is wired,
     * report RSSI as unknown (-128); disconnect events invalidate the cache. */
    int rssi = atomic_load_explicit(&s_rssi, memory_order_relaxed);
    unsigned cpu = 0U;
    unsigned stack_low = 0U;
    unsigned tasks = 0U;
    char stack_low_task[configMAX_TASK_NAME_LEN + 1U] = "-";
    /* The SMP task-state scan holds xKernelLock while walking every
     * task.  It must not run in periodic telemetry: a stalled walk blocks
     * heartbeat scheduling on the other AP core.  The task count is a single
     * atomic-width read in this SDK; CPU and stack figures are unavailable. */
    tasks = (unsigned)uxTaskGetNumberOfTasks();
    int cpu_rc = BK_FAIL;
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
    rtos_lock_mutex(&s_state_mutex);
    s_snapshot.sampled = cpu_rc == BK_OK;
    s_snapshot.cpu_percent = cpu;
    s_snapshot.task_count = tasks;
    s_snapshot.stack_low_words = stack_low;
    snprintf(s_snapshot.stack_low_task, sizeof(s_snapshot.stack_low_task), "%s", stack_low_task);
    rtos_unlock_mutex(&s_state_mutex);
    bk_printf_raw_nonblock(BK_LOG_NONE, NULL, "HBTRACE metrics-log-enter\n");
    /* The shell's common log mode can wait for buffer space. This periodic
     * task must never hold that path while AP heartbeat scheduling depends on
     * the same SMP kernel. A full log buffer may drop this sample. */
    bk_printf_nonblock(BK_LOG_INFO, TAG,
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
    bk_printf_raw_nonblock(BK_LOG_NONE, NULL, "HBTRACE metrics-log-exit\n");
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
