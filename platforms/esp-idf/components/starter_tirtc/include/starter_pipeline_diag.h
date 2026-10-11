#pragma once
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
/* Fixed storage, no packet contents or peer addresses. An epoch prevents an
 * in-flight operation from being attributed after disable/reset/re-enable. */
typedef enum {
    PIPELINE_AUDIO_SUBMIT, PIPELINE_VIDEO_SUBMIT,
    PIPELINE_AUDIO_CALLBACK, PIPELINE_VIDEO_CALLBACK,
    PIPELINE_SOCKET_SEND, PIPELINE_SOCKET_SENDTO,
    PIPELINE_SOCKET_RECV, PIPELINE_SOCKET_RECVFROM,
    PIPELINE_SOCKET_SENDMSG,
    PIPELINE_STAGE_COUNT
} pipeline_diag_stage_t;
typedef struct { uint64_t started_us; uint32_t epoch; } pipeline_diag_ticket_t;
typedef struct {
    uint64_t calls, bytes, failures, total_us, max_us;
    uint64_t last_event_us, gap_total_us, gap_max_us, gaps;
    int last_error;
} pipeline_diag_metric_t;
/* Portable accumulator; caller owns synchronization. Successful observations
 * establish cadence, failed polling attempts cannot hide an arrival gap. */
static inline void pipeline_diag_accumulate(pipeline_diag_metric_t *m,
    uint64_t started, uint64_t ended, uint64_t event, size_t bytes, int error)
{
    uint64_t elapsed = ended >= started ? ended - started : 0;
    m->calls++; m->total_us += elapsed;
    if (elapsed > m->max_us) m->max_us = elapsed;
    if (error) { m->failures++; m->last_error = error; return; }
    m->bytes += bytes;
    if (m->last_event_us && event >= m->last_event_us) {
        uint64_t gap = event - m->last_event_us;
        m->gaps++; m->gap_total_us += gap;
        if (gap > m->gap_max_us) m->gap_max_us = gap;
    }
    /* Concurrent completions may arrive out of order. Never move backwards. */
    if (event > m->last_event_us) m->last_event_us = event;
}
void starter_pipeline_diag_set_enabled(bool enabled);
bool starter_pipeline_diag_enabled(void);
void starter_pipeline_diag_reset(void);
pipeline_diag_ticket_t starter_pipeline_diag_begin(void);
void starter_pipeline_diag_end(pipeline_diag_ticket_t ticket,
    pipeline_diag_stage_t stage, size_t bytes, int error, bool completion_event);
void starter_pipeline_diag_sample_buffer(size_t used);
void starter_pipeline_diag_report(bool reset_window);
