#include "starter_ai_playout.h"
#include "xiaotai_audio_buffer.h"
#include "xiaotai_media_policy.h"
#include "freertos/FreeRTOS.h"
#include "freertos/portmacro.h"

/* Bounded scalar operations only. No decoder, allocation, I2S or RTOS wait is
 * allowed inside this lock; callbacks can run concurrently with the sink. */
static portMUX_TYPE s_lock = portMUX_INITIALIZER_UNLOCKED;
static uint32_t s_generation;
static uint32_t s_queued_samples;
static uint32_t s_waits;
static bool s_ended;
static xiaotai_audio_buffer_t s_buffer;

static uint16_t opus_samples_16k(const uint8_t *packet, size_t size)
{
    /* RFC 6716 sections 3.1 and 3.2: configuration selects frame duration,
     * framing code selects frame count. No decoding or payload copy here.
     * https://www.rfc-editor.org/rfc/rfc6716#section-3.1 */
    static const uint16_t samples_per_frame[32] = {
        160,320,640,960, 160,320,640,960, 160,320,640,960,
        160,320,160,320, 40,80,160,320, 40,80,160,320,
        40,80,160,320, 40,80,160,320,
    };
    if (packet == NULL || size == 0U) return 0U;
    unsigned code = packet[0] & 3U;
    if (code == 3U && size < 2U) return 0U;
    unsigned count = code == 0U ? 1U : code == 3U ? packet[1] & 63U : 2U;
    unsigned samples = samples_per_frame[packet[0] >> 3] * count;
    return samples != 0U && samples <= 1920U ? (uint16_t)samples : 0U;
}

void starter_ai_playout_reset(uint32_t generation)
{
    portENTER_CRITICAL(&s_lock);
    s_generation = generation;
    s_queued_samples = 0U;
    s_waits = 0U;
    s_ended = false;
    xiaotai_audio_buffer_reset(&s_buffer);
    portEXIT_CRITICAL(&s_lock);
}

uint16_t starter_ai_playout_admit(uint32_t generation,
                                 const uint8_t *packet, size_t size)
{
    uint16_t samples = opus_samples_16k(packet, size);
    if (samples == 0U) return 0U;
    portENTER_CRITICAL(&s_lock);
    if (generation == 0U || generation != s_generation ||
        s_queued_samples > XIAOTAI_AI_PACKET_CAPACITY * 1920U - samples) {
        samples = 0U;
    } else {
        s_queued_samples += samples;
    }
    portEXIT_CRITICAL(&s_lock);
    return samples;
}

void starter_ai_playout_retire(uint32_t generation, uint16_t samples)
{
    portENTER_CRITICAL(&s_lock);
    if (generation != 0U && generation == s_generation) {
        s_queued_samples = samples <= s_queued_samples ? s_queued_samples - samples : 0U;
    }
    portEXIT_CRITICAL(&s_lock);
}

bool starter_ai_playout_ready(uint32_t generation, uint32_t now_ms,
                             uint32_t first_rx_ms, uint32_t dma_tail_ms)
{
    bool ready = false;
    portENTER_CRITICAL(&s_lock);
    if (generation != 0U && generation == s_generation) {
        bool waiting = s_buffer.waiting;
        ready = xiaotai_audio_buffer_ready(&s_buffer, now_ms,
            s_queued_samples / 16U, first_rx_ms, dma_tail_ms, s_ended);
        if (!waiting && s_buffer.waiting) ++s_waits;
    }
    portEXIT_CRITICAL(&s_lock);
    return ready;
}

void starter_ai_playout_written(uint32_t generation, size_t samples)
{
    portENTER_CRITICAL(&s_lock);
    if (generation != 0U && generation == s_generation && samples <= 1920U)
        xiaotai_audio_buffer_written(&s_buffer, (uint32_t)samples / 16U);
    portEXIT_CRITICAL(&s_lock);
}

void starter_media_ai_playback_boundary(uint32_t generation, bool ended)
{
    portENTER_CRITICAL(&s_lock);
    if (generation != 0U && generation == s_generation) {
        if (!ended) {
            if (s_ended) xiaotai_audio_buffer_begin_round(&s_buffer);
            /* A new round must not learn from the preceding turn's silence.
             * A duplicate start must not reset an already waiting deadline. */
            s_buffer.recovering = false;
        }
        s_ended = ended;
    }
    portEXIT_CRITICAL(&s_lock);
}

starter_ai_playout_status_t starter_ai_playout_status(void)
{
    portENTER_CRITICAL(&s_lock);
    starter_ai_playout_status_t result = {
        .queued_samples = s_queued_samples,
        .target_ms = s_buffer.target_ms,
        .underruns = s_buffer.underruns,
        .waits = s_waits,
        .timeout_releases = s_buffer.timeout_releases,
    };
    portEXIT_CRITICAL(&s_lock);
    return result;
}
