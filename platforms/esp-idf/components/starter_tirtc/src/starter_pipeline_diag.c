#include "starter_pipeline_diag.h"
#include "sdkconfig.h"
#if CONFIG_STARTER_PIPELINE_DIAGNOSTICS
#include <stdatomic.h>
#include <stdio.h>
#include <string.h>
#include <inttypes.h>
#include <errno.h>
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "lwip/sockets.h"
static portMUX_TYPE lock = portMUX_INITIALIZER_UNLOCKED;
static atomic_bool enabled;
static uint32_t epoch = 1;
static uint64_t window_start;
static pipeline_diag_metric_t metrics[PIPELINE_STAGE_COUNT];
static size_t buffer_first, buffer_last, buffer_peak;
static uint64_t buffer_first_us, buffer_last_us;
static unsigned buffer_samples;
static void reset_locked(void)
{
    memset(metrics, 0, sizeof(metrics));
    buffer_first = buffer_last = buffer_peak = buffer_samples = 0;
    buffer_first_us = buffer_last_us = 0;
    window_start = esp_timer_get_time();
    if (++epoch == 0) epoch = 1;
}
void starter_pipeline_diag_set_enabled(bool value)
{
    taskENTER_CRITICAL(&lock);
    atomic_store(&enabled, false);
    reset_locked();
    atomic_store(&enabled, value);
    taskEXIT_CRITICAL(&lock);
}
bool starter_pipeline_diag_enabled(void) { return atomic_load(&enabled); }
void starter_pipeline_diag_reset(void)
{
    taskENTER_CRITICAL(&lock); reset_locked(); taskEXIT_CRITICAL(&lock);
}
pipeline_diag_ticket_t starter_pipeline_diag_begin(void)
{
    pipeline_diag_ticket_t ticket = {0};
    if (!atomic_load(&enabled)) return ticket;
    taskENTER_CRITICAL(&lock);
    if (atomic_load(&enabled)) {
        ticket.epoch = epoch; ticket.started_us = esp_timer_get_time();
    }
    taskEXIT_CRITICAL(&lock); return ticket;
}
void starter_pipeline_diag_end(pipeline_diag_ticket_t ticket,
    pipeline_diag_stage_t stage, size_t bytes, int error, bool completion_event)
{
    if (!ticket.epoch || (unsigned)stage >= PIPELINE_STAGE_COUNT || !atomic_load(&enabled)) return;
    uint64_t ended = esp_timer_get_time();
    taskENTER_CRITICAL(&lock);
    if (ticket.epoch == epoch && atomic_load(&enabled))
        pipeline_diag_accumulate(&metrics[stage], ticket.started_us, ended,
            completion_event ? ended : ticket.started_us, bytes, error);
    taskEXIT_CRITICAL(&lock);
}
void starter_pipeline_diag_sample_buffer(size_t used)
{
    if (!atomic_load(&enabled)) return;
    uint64_t now = esp_timer_get_time();
    taskENTER_CRITICAL(&lock);
    if (atomic_load(&enabled)) {
        if (!buffer_samples) { buffer_first = used; buffer_first_us = now; }
        buffer_last = used; buffer_last_us = now; buffer_samples++;
        if (used > buffer_peak) buffer_peak = used;
    }
    taskEXIT_CRITICAL(&lock);
}
void starter_pipeline_diag_report(bool reset_window)
{
    pipeline_diag_metric_t copy[PIPELINE_STAGE_COUNT];
    uint64_t start, now = esp_timer_get_time(), span;
    size_t first, last, peak; unsigned samples;
    taskENTER_CRITICAL(&lock);
    memcpy(copy, metrics, sizeof(copy)); start = window_start;
    first = buffer_first; last = buffer_last; peak = buffer_peak; samples = buffer_samples;
    span = buffer_last_us >= buffer_first_us ? buffer_last_us - buffer_first_us : 0;
    if (reset_window) reset_locked();
    taskEXIT_CRITICAL(&lock);
    const char *names[] = {"sdk-audio-submit", "sdk-video-submit", "audio-callback",
        "video-callback", "socket-send", "socket-sendto", "socket-recv", "socket-recvfrom"};
    printf("Media diagnostic: enabled=%d window-ms=%" PRIu64 " socket-scope=all-lwip; recv-us includes blocking wait\n",
        starter_pipeline_diag_enabled(), start && now >= start ? (now-start)/1000 : 0);
    for (unsigned i=0; i<PIPELINE_STAGE_COUNT; i++) {
        pipeline_diag_metric_t *m=&copy[i];
        printf("DIAG %s n=%" PRIu64 " bytes=%" PRIu64 " fail=%" PRIu64
            " call-us=avg:%" PRIu64 "/max:%" PRIu64 " gap-us=avg:%" PRIu64 "/max:%" PRIu64 " last-error=%d\n",
            names[i],m->calls,m->bytes,m->failures,m->calls?m->total_us/m->calls:0,m->max_us,
            m->gaps?m->gap_total_us/m->gaps:0,m->gap_max_us,m->last_error);
    }
    int64_t delta = (int64_t)last - (int64_t)first;
    printf("DIAG sdk-buffer samples=%u first=%u last=%u peak=%u net-bytes-per-sec=%" PRId64 "\n",
        samples,(unsigned)first,(unsigned)last,(unsigned)peak,
        span ? delta*1000000/(int64_t)span : 0);
}
/* Link-time observation at the real lwIP boundary. Preserve return values,
 * flags, addresses, lengths and errno. No retries or socket option changes.
 * These counters include unrelated sockets; they are not C6 radio delivery. */
#define WRAP_SOCKET(name, stage, parameters, arguments) \
extern ssize_t __real_lwip_##name parameters; \
ssize_t __wrap_lwip_##name parameters { \
    pipeline_diag_ticket_t t=starter_pipeline_diag_begin(); \
    ssize_t result=__real_lwip_##name arguments; int saved=errno; \
    starter_pipeline_diag_end(t,stage,result>0?(size_t)result:0,result<0?saved:0,true); \
    errno=saved;return result; \
}
WRAP_SOCKET(send, PIPELINE_SOCKET_SEND,
 (int s,const void *data,size_t size,int flags),(s,data,size,flags))
WRAP_SOCKET(sendto, PIPELINE_SOCKET_SENDTO,
 (int s,const void *data,size_t size,int flags,const struct sockaddr *to,socklen_t len),(s,data,size,flags,to,len))
WRAP_SOCKET(recv, PIPELINE_SOCKET_RECV,
 (int s,void *data,size_t size,int flags),(s,data,size,flags))
WRAP_SOCKET(recvfrom, PIPELINE_SOCKET_RECVFROM,
 (int s,void *data,size_t size,int flags,struct sockaddr *from,socklen_t *len),(s,data,size,flags,from,len))
#else
void starter_pipeline_diag_set_enabled(bool value) {(void)value;}
bool starter_pipeline_diag_enabled(void) {return false;}
void starter_pipeline_diag_reset(void) {}
pipeline_diag_ticket_t starter_pipeline_diag_begin(void) {return (pipeline_diag_ticket_t){0};}
void starter_pipeline_diag_end(pipeline_diag_ticket_t t,pipeline_diag_stage_t s,size_t b,int e,bool c)
{(void)t;(void)s;(void)b;(void)e;(void)c;}
void starter_pipeline_diag_sample_buffer(size_t used) {(void)used;}
void starter_pipeline_diag_report(bool reset_window) {(void)reset_window;}
#endif
