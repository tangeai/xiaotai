#include "xiaotai_audio.h"
#include "xiaotai_prompt_assets.h"
#include "starter_audio_resampler.h"
#include "xiaotai_audio_agc.h"
#include "xiaotai_audio_ns.h"
#include "xiaotai_audio_policy.h"
#include "board_audio.h"

#include <common/bk_err.h>
#include "xiaotai_log.h"
#include <modules/aec.h>
#include <modules/g711.h>
#include <modules/opus.h>
#include <os/mem.h>
#include <os/os.h>
#include <stdatomic.h>
#include <string.h>

#define TAG "xiaotai_audio"

#define AUDIO_RING_FRAMES 8U
#define AUDIO_MAX_FRAME_SAMPLES XIAOTAI_AUDIO_OPUS_FRAME_SAMPLES
#define AUDIO_ALAW_FRAME_BYTES XIAOTAI_AUDIO_ALAW_FRAME_SAMPLES
#define AUDIO_RING_BYTES (AUDIO_RING_FRAMES * AUDIO_ALAW_FRAME_BYTES)
#define AUDIO_TASK_PRIORITY 5
#define AUDIO_TASK_STACK 4096
/* The fixed-point Opus encoder's SILK pitch-analysis path exceeded 24 KiB on
 * BK7258 (hardware stack guard, PC in silk_pitch_analysis_core). Keep useful
 * headroom instead of sizing this PSRAM-backed task at the observed limit. */
#define OPUS_CAPTURE_TASK_STACK (40U * 1024U)
/* The Opus decoder's CELT MDCT/FFT path exhausted the previous 10 KiB stack;
 * the crash PSP was exactly at the task's lower stack boundary. */
#define OPUS_PLAYBACK_TASK_STACK (40U * 1024U)
#define AUDIO_TASK_STOP_TIMEOUT_MS 1000U
#define OPUS_SAMPLE_RATE_HZ 16000U
#define OPUS_FRAME_SAMPLES XIAOTAI_AUDIO_OPUS_FRAME_SAMPLES
#define OPUS_BITRATE_BPS 16000
#define OPUS_MAX_PACKET_BYTES 512U
/* Retain AI TTS bursts ahead of the DAC, without delaying playback. */
#define OPUS_PACKET_SLOTS 64U
#define OPUS_MAX_DECODE_SAMPLES 960U
#define AEC_DELAY_SAMPLE_POINTS_MAX 1000U
#define AEC_DELAY_POINTS 211U
#define AEC_REFERENCE_FRAMES 16U
#define AEC_MAX_FRAME_SAMPLES OPUS_FRAME_SAMPLES
#define AEC_CAPTURE_TASK_STACK (8U * 1024U)
#define ECHO_DIAG_INTERVAL_FRAMES 100U
#define UPLINK_LEVEL_LOG_INTERVAL_FRAMES 250U
#define AI_NS_SUPPRESSION_DB (-25)

static const xiaotai_board_audio_adapter_t *s_board_audio;
static beken_thread_t s_capture_thread;
static beken_thread_t s_play_thread;
static beken_semaphore_t s_capture_stopped;
static beken_semaphore_t s_play_stopped;
static bool s_capture_joined;
static bool s_play_joined;
static beken_mutex_t s_ring_mutex;
static bool s_ring_mutex_ready;
static atomic_bool s_capture_run;
static atomic_bool s_play_run;
static atomic_bool s_running;
static atomic_bool s_playback_in_progress;
static atomic_uint s_last_downlink_ms;
static atomic_uint s_playback_tail_ms;
static atomic_uint s_opus_received_frames;
static atomic_uint s_admission_busy;
static atomic_uint s_playback_pcm_ms;
static atomic_uint s_playback_columns;
static atomic_uint s_playback_columns_at_ms;
static atomic_uint s_last_opus_rx_ms;
static atomic_bool s_prompt_run;
static atomic_bool s_prompt_active;
static atomic_uint s_volume = 7U;
static atomic_bool s_speaker_muted;
static atomic_bool s_microphone_muted;
static atomic_bool s_uplink_enabled = true;

/* Seven short-time amplitude slices drive the compact dot mouth. This is a
 * display hint only; the audio worker never waits for the UI. */
static uint32_t playback_columns_pack(const int16_t *pcm, size_t samples)
{
    if (pcm == NULL || samples == 0U) return 0U;
    static const uint16_t thresholds[7] = {
        160U, 320U, 640U, 1200U, 2400U, 4800U, 9600U,
    };
    uint32_t packed = 0U;
    for (size_t column = 0U; column < 7U; ++column) {
        size_t begin = column * samples / 7U;
        size_t end = (column + 1U) * samples / 7U;
        uint32_t peak = 0U;
        for (size_t i = begin; i < end; i += 2U) {
            int32_t value = pcm[i];
            uint32_t magnitude = (uint32_t)(value < 0 ? -value : value);
            if (magnitude > peak) peak = magnitude;
        }
        uint32_t height = 0U;
        while (height < 7U && peak >= thresholds[height]) ++height;
        packed |= height << (column * 4U);
    }
    return packed;
}
static xiaotai_audio_codec_t s_codec = XIAOTAI_AUDIO_CODEC_ALAW_8K;
static size_t s_frame_samples = XIAOTAI_AUDIO_ALAW_FRAME_SAMPLES;
static xiaotai_audio_agc_t s_uplink_agc;
static xiaotai_audio_ns_t s_uplink_ns;
/* s_codec continues to describe hardware/DSP and downlink; uplink is separate. */
static xiaotai_audio_codec_t s_uplink_codec;
static starter_audio_resampler_16k_to_8k_t s_uplink_resampler;
static OpusEncoder *s_opus_encoder;
static OpusDecoder *s_opus_decoder;
static AECContext *s_aec;
static int16_t *s_aec_mic;
static int16_t *s_aec_ref;
static int16_t *s_aec_out;
static int16_t *s_aec_reference_ring;
static uint32_t s_aec_context_bytes;
static size_t s_aec_frame_samples;
static size_t s_aec_reference_head;
static size_t s_aec_reference_count;
static int16_t s_aec_reference_partial[AEC_MAX_FRAME_SAMPLES];
static size_t s_aec_reference_partial_count;
static uint32_t s_aec_processed_frames;
static uint32_t s_aec_reference_underruns;
static uint32_t s_aec_reference_overruns;
static uint64_t s_echo_diag_mic_abs_sum;
static uint64_t s_echo_diag_ref_abs_sum;
static uint64_t s_echo_diag_out_abs_sum;
static uint32_t s_echo_diag_samples;
static uint32_t s_echo_diag_frames;
static uint32_t s_echo_diag_mic_peak;
static uint32_t s_echo_diag_ref_peak;
static uint32_t s_echo_diag_out_peak;
static uint32_t s_echo_diag_last_underruns;
static uint32_t s_echo_diag_last_overruns;
static beken_thread_t s_prompt_thread;
static beken_semaphore_t s_prompt_stopped;
static bool s_prompt_sync_ready;
static int16_t *s_prompt_pcm;
static size_t s_prompt_samples;
static unsigned s_prompt_repeats;
static atomic_int s_prompt_result;
static atomic_uint s_prompt_generation;
static atomic_int s_call_ring_kind;
static atomic_uint s_call_ring_started_ms;
static unsigned s_ring_prompt_generation;
static unsigned s_ring_prompt_volume;
/* Serialize prompt creation and media device transitions on SMP. Workers never
 * take this lock, so cancellation can wait for their completion safely. */
static atomic_flag s_audio_control_lock = ATOMIC_FLAG_INIT;

static bool audio_control_enter(bool wait)
{
    for (unsigned waited = 0U;; ++waited) {
        if (!atomic_flag_test_and_set_explicit(&s_audio_control_lock,
                                               memory_order_acquire)) return true;
        if (!wait || waited >= AUDIO_TASK_STOP_TIMEOUT_MS) return false;
        rtos_delay_milliseconds(1);
    }
}

static void audio_control_leave(void)
{
    atomic_flag_clear_explicit(&s_audio_control_lock, memory_order_release);
}

static xiaotai_audio_uplink_fn s_uplink;
static void *s_uplink_context;

static uint8_t s_alaw_ring[AUDIO_RING_BYTES];
static uint8_t *s_opus_ring;
static uint16_t s_opus_lengths[OPUS_PACKET_SLOTS];
static size_t s_opus_head;
static size_t s_opus_count;
static size_t s_opus_queue_peak;
static size_t s_ring_head;
static size_t s_ring_count;
static uint32_t s_dropped_bytes;
static uint32_t s_drop_reported_bytes;
static uint32_t s_uplink_frames;
static uint32_t s_uplink_rejected;
static uint32_t s_uplink_level_frames;
static uint32_t s_uplink_agc_failures;
static uint32_t s_uplink_ns_speech_frames;
static uint32_t s_uplink_ns_noise_frames;
static uint32_t s_uplink_ns_failures;
static atomic_uint s_downlink_frames;

static bool play_prompt_pcm(void)
{
    size_t consumed = 0;
    while (consumed < s_prompt_samples && s_prompt_run) {
        size_t samples = s_prompt_samples - consumed;
        if (samples > AUDIO_MAX_FRAME_SAMPLES) {
            samples = AUDIO_MAX_FRAME_SAMPLES;
        }
        if (xiaotai_board_prompt_audio_write(s_prompt_pcm + consumed,
                                             samples) < 0) {
            return false;
        }
        consumed += samples;
    }
    return s_prompt_run;
}

static void prompt_task(beken_thread_arg_t argument)
{
    (void)argument;
    int result = xiaotai_board_prompt_audio_start();
    bool ready = result == BK_OK;
    if (!ready) BK_LOGE(TAG, "cannot open speaker for audio prompt\n");

    if (ready) {
        for (unsigned repeat = 0; repeat < s_prompt_repeats && s_prompt_run; ++repeat) {
            if (!play_prompt_pcm()) {
                result = BK_FAIL;
                break;
            }
            if (repeat + 1U < s_prompt_repeats) {
                for (unsigned waited = 0; waited < 700U && s_prompt_run; waited += 20U)
                    rtos_delay_milliseconds(20);
            }
        }
    }
    xiaotai_board_prompt_audio_stop();
    if (s_prompt_pcm != NULL) {
        memset(s_prompt_pcm, 0, s_prompt_samples * sizeof(int16_t));
        psram_free(s_prompt_pcm);
        s_prompt_pcm = NULL;
        s_prompt_samples = 0U;
    }
    atomic_store(&s_prompt_result, result);
    atomic_store_explicit(&s_prompt_run, false, memory_order_release);
    s_prompt_thread = NULL;
    rtos_set_semaphore(&s_prompt_stopped);
    atomic_store_explicit(&s_prompt_active, false, memory_order_release);
    BK_LOGI(TAG, "audio prompt stopped\n");
    rtos_delete_thread(NULL);
}

bool xiaotai_audio_prompt_result(int *result)
{
    if (result == NULL || atomic_load(&s_prompt_active)) return false;
    *result = atomic_load(&s_prompt_result);
    return true;
}

static void cancel_prompt_locked(void)
{
    if (!atomic_load_explicit(&s_prompt_active, memory_order_acquire)) return;
    atomic_store_explicit(&s_prompt_run, false, memory_order_release);
    if (s_prompt_sync_ready) {
        int rc = rtos_get_semaphore(&s_prompt_stopped,
                                     AUDIO_TASK_STOP_TIMEOUT_MS);
        /* The worker signals before publishing inactive. On SMP, waking from
         * the semaphore alone does not mean that publication is visible yet. */
        for (unsigned waited = 0U; rc == BK_OK &&
             atomic_load_explicit(&s_prompt_active, memory_order_acquire) &&
             waited < AUDIO_TASK_STOP_TIMEOUT_MS; ++waited)
            rtos_delay_milliseconds(1);
    }
}

void xiaotai_audio_cancel_prompt(void)
{
    if (!audio_control_enter(true)) return;
    cancel_prompt_locked();
    audio_control_leave();
}

static int start_prompt_locked(int16_t *pcm, size_t sample_count, unsigned repeats)
{
    /* 250 ms to 20 seconds is deliberately bounded.  The API contract is
     * 8 kHz mono S16LE, so longer or empty responses are not accepted. */
    if (pcm == NULL || sample_count < 2000U || sample_count > 160000U) {
        return BK_ERR_PARAM;
    }
    if (s_running ||
        atomic_load_explicit(&s_prompt_active, memory_order_acquire)) {
        return BK_ERR_BUSY;
    }
    if (!s_prompt_sync_ready) {
        int rc = rtos_init_semaphore(&s_prompt_stopped, 1);
        if (rc != BK_OK) return rc;
        s_prompt_sync_ready = true;
    } else {
        (void)rtos_get_semaphore(&s_prompt_stopped, 0);
    }
    atomic_fetch_add(&s_prompt_generation, 1U);
    s_prompt_pcm = pcm;
    s_prompt_samples = sample_count;
    s_prompt_repeats = repeats;
    atomic_store_explicit(&s_prompt_run, true, memory_order_release);
    atomic_store_explicit(&s_prompt_active, true, memory_order_release);
    int rc = rtos_create_psram_thread(&s_prompt_thread, AUDIO_TASK_PRIORITY,
                                      "xiaotai_prompt", prompt_task,
                                      AUDIO_TASK_STACK, NULL);
    if (rc != BK_OK) {
        atomic_store_explicit(&s_prompt_run, false, memory_order_release);
        atomic_store_explicit(&s_prompt_active, false, memory_order_release);
        s_prompt_pcm = NULL;
        s_prompt_samples = 0U;
    }
    return rc;
}

static int start_prompt(int16_t *pcm, size_t sample_count, unsigned repeats)
{
    if (!audio_control_enter(false)) return BK_ERR_BUSY;
    int rc = start_prompt_locked(pcm, sample_count, repeats);
    audio_control_leave();
    return rc;
}

int xiaotai_audio_announce_verification_pcm(int16_t *pcm, size_t sample_count)
{
    return start_prompt(pcm, sample_count, 3U);
}

int xiaotai_audio_preview_acknowledgement(bool male)
{
    if (s_running || s_speaker_muted || atomic_load(&s_prompt_active))
        return BK_ERR_BUSY;
    const int16_t *source = male ? xiaotai_ack_male : xiaotai_ack_female;
    size_t bytes = male ? sizeof(xiaotai_ack_male) : sizeof(xiaotai_ack_female);
    int16_t *pcm = psram_malloc(bytes);
    if (pcm == NULL) return BK_ERR_NO_MEM;
    memcpy(pcm, source, bytes);
    int rc = start_prompt(pcm, bytes / sizeof(*pcm), 1U);
    if (rc != BK_OK) psram_free(pcm);
    return rc;
}

/* Only the control task owns prompt generation; playback reads the atomic view. */
static const int16_t *call_ringtone_pcm(xiaotai_call_ring_t kind, size_t *samples)
{
    if (kind == XIAOTAI_CALL_RING_INCOMING) {
        *samples = sizeof(xiaotai_ring_incoming) / sizeof(int16_t);
        return xiaotai_ring_incoming;
    }
    *samples = sizeof(xiaotai_ring_outgoing) / sizeof(int16_t);
    return xiaotai_ring_outgoing;
}

static void service_call_ringtone_locked(xiaotai_call_ring_t kind)
{
    if (kind < XIAOTAI_CALL_RING_NONE || kind > XIAOTAI_CALL_RING_OUTGOING) return;
    /* Call-active media wins even before the runtime callback is serviced. */
    if (kind == XIAOTAI_CALL_RING_OUTGOING && s_running)
        kind = XIAOTAI_CALL_RING_NONE;
    unsigned volume = atomic_load(&s_volume);
    bool muted = atomic_load(&s_speaker_muted) || volume == 0U;
    bool changed = kind != atomic_load(&s_call_ring_kind);
    bool owns_prompt = s_ring_prompt_generation != 0U &&
                       s_ring_prompt_generation == atomic_load(&s_prompt_generation);
    if (owns_prompt && (changed || kind == XIAOTAI_CALL_RING_NONE || muted || s_running ||
                        volume != s_ring_prompt_volume)) {
        cancel_prompt_locked();
        if (!atomic_load_explicit(&s_prompt_active, memory_order_acquire))
            s_ring_prompt_generation = 0U;
    }
    if (changed) {
        atomic_store(&s_call_ring_started_ms, rtos_get_time());
        atomic_store_explicit(&s_call_ring_kind, kind, memory_order_release);
        BK_LOGI(TAG, "call ringtone %s\n",
                kind == XIAOTAI_CALL_RING_INCOMING ? "incoming" :
                kind == XIAOTAI_CALL_RING_OUTGOING ? "outgoing" : "stopped");
    }
    if (kind == XIAOTAI_CALL_RING_NONE || s_running || muted ||
        atomic_load_explicit(&s_prompt_active, memory_order_acquire)) return;
    size_t samples;
    const int16_t *source = call_ringtone_pcm(kind, &samples);
    int16_t *pcm = psram_malloc(samples * sizeof(*pcm));
    if (pcm == NULL) return;
    for (size_t i = 0; i < samples; ++i)
        pcm[i] = (int16_t)((int32_t)source[i] * (int32_t)volume / 10);
    int rc = start_prompt_locked(pcm, samples, 1U);
    if (rc != BK_OK) {
        psram_free(pcm);
    } else {
        s_ring_prompt_generation = atomic_load(&s_prompt_generation);
        s_ring_prompt_volume = volume;
    }
}

void xiaotai_audio_service_call_ringtone(xiaotai_call_ring_t kind)
{
    /* A busy handoff is retried by the next control maintenance iteration. */
    if (!audio_control_enter(false)) return;
    service_call_ringtone_locked(kind);
    audio_control_leave();
}

/* Called only by the existing playback task, including gaps in remote audio.
 * Mix before volume/mute and feed the resulting speaker PCM to AEC. */
static void mix_call_ringtone(int16_t *pcm, size_t samples, uint32_t rate)
{
    xiaotai_call_ring_t kind = atomic_load_explicit(&s_call_ring_kind, memory_order_acquire);
    if (kind == XIAOTAI_CALL_RING_NONE) return;
    size_t length;
    const int16_t *ring = call_ringtone_pcm(kind, &length);
    uint32_t elapsed = rtos_get_time() - atomic_load(&s_call_ring_started_ms);
    size_t period = length + 5600U; /* 700 ms between repetitions, at 8 kHz. */
    size_t position = ((uint64_t)elapsed * 8U) % period;
    for (size_t i = 0; i < samples; ++i) {
        size_t index = (position + i * 8000U / rate) % period;
        int32_t value = pcm[i] / 2 + (index < length ? ring[index] : 0);
        if (value > 32767) value = 32767;
        if (value < -32768) value = -32768;
        pcm[i] = (int16_t)value;
    }
}

static void ring_reset(void)
{
    if (!s_ring_mutex_ready) return;
    rtos_lock_mutex(&s_ring_mutex);
    s_ring_head = 0;
    s_ring_count = 0;
    s_opus_head = 0;
    s_opus_count = 0;
    s_aec_reference_head = 0;
    s_aec_reference_count = 0;
    s_aec_reference_partial_count = 0;
    rtos_unlock_mutex(&s_ring_mutex);
}

static void aec_destroy(void)
{
    s_aec_mic = NULL;
    s_aec_ref = NULL;
    s_aec_out = NULL;
    s_aec_frame_samples = 0U;
    if (s_aec_reference_ring != NULL) {
        psram_free(s_aec_reference_ring);
        s_aec_reference_ring = NULL;
    }
    if (s_aec != NULL) {
        memset(s_aec, 0, s_aec_context_bytes);
        psram_free(s_aec);
        s_aec = NULL;
    }
    s_aec_context_bytes = 0U;
}

static int aec_start(uint32_t sample_rate_hz)
{
    uint32_t value = 0U;
    uint32_t frame_samples = 0U;

    s_aec_context_bytes = aec_size(AEC_DELAY_SAMPLE_POINTS_MAX);
    s_aec = psram_malloc(s_aec_context_bytes);
    if (s_aec == NULL) return BK_ERR_NO_MEM;
    memset(s_aec, 0, s_aec_context_bytes);
    aec_init(s_aec, (int16_t)sample_rate_hz);
    aec_ctrl(s_aec, AEC_CTRL_CMD_GET_FRAME_SAMPLE,
             (uint32_t)(uintptr_t)&frame_samples);
    if (frame_samples == 0U || frame_samples > AEC_MAX_FRAME_SAMPLES ||
        frame_samples != sample_rate_hz / 50U) {
        BK_LOGE(TAG, "AEC returned invalid 20ms frame=%u rate=%u\n",
                (unsigned)frame_samples, (unsigned)sample_rate_hz);
        aec_destroy();
        return BK_FAIL;
    }

    aec_ctrl(s_aec, AEC_CTRL_CMD_GET_TX_BUF, (uint32_t)(uintptr_t)&value);
    s_aec_mic = (int16_t *)(uintptr_t)value;
    aec_ctrl(s_aec, AEC_CTRL_CMD_GET_RX_BUF, (uint32_t)(uintptr_t)&value);
    s_aec_ref = (int16_t *)(uintptr_t)value;
    aec_ctrl(s_aec, AEC_CTRL_CMD_GET_OUT_BUF, (uint32_t)(uintptr_t)&value);
    s_aec_out = (int16_t *)(uintptr_t)value;
    if (s_aec_mic == NULL || s_aec_ref == NULL || s_aec_out == NULL) {
        BK_LOGE(TAG, "AEC working buffers unavailable\n");
        aec_destroy();
        return BK_FAIL;
    }

    s_aec_frame_samples = frame_samples;
    s_aec_reference_ring = psram_malloc(AEC_REFERENCE_FRAMES *
                                         s_aec_frame_samples *
                                         sizeof(*s_aec_reference_ring));
    if (s_aec_reference_ring == NULL) {
        aec_destroy();
        return BK_ERR_NO_MEM;
    }
    memset(s_aec_reference_ring, 0,
           AEC_REFERENCE_FRAMES * s_aec_frame_samples *
           sizeof(*s_aec_reference_ring));

    /* Values are the official BK AEC algorithm defaults.  The playback
     * reference below is the post-volume PCM actually submitted to the DAC;
     * enclosure-specific delay tuning remains a hardware validation step. */
    aec_ctrl(s_aec, AEC_CTRL_CMD_SET_FLAGS, 0x1fU);
    aec_ctrl(s_aec, AEC_CTRL_CMD_SET_MIC_DELAY, AEC_DELAY_POINTS);
    aec_ctrl(s_aec, AEC_CTRL_CMD_SET_EC_DEPTH, 50U);
    aec_ctrl(s_aec, AEC_CTRL_CMD_SET_TxRxThr, 30U);
    aec_ctrl(s_aec, AEC_CTRL_CMD_SET_TxRxFlr, 6U);
    aec_ctrl(s_aec, AEC_CTRL_CMD_SET_REF_SCALE, 0U);
    aec_ctrl(s_aec, AEC_CTRL_CMD_SET_VOL, 14U);
    aec_ctrl(s_aec, AEC_CTRL_CMD_SET_NS_LEVEL, 2U);
    aec_ctrl(s_aec, AEC_CTRL_CMD_SET_NS_PARA, 1U);
    aec_ctrl(s_aec, AEC_CTRL_CMD_SET_DRC, 0x15U);
    BK_LOGI(TAG, "AEC ready rate=%u frame=%u context=%u delay=%u\n",
            (unsigned)sample_rate_hz, (unsigned)s_aec_frame_samples,
            (unsigned)s_aec_context_bytes, (unsigned)AEC_DELAY_POINTS);
    return BK_OK;
}

static void aec_reference_store_frame_locked(const int16_t *frame)
{
    if (s_aec_reference_count == AEC_REFERENCE_FRAMES) {
        s_aec_reference_head =
            (s_aec_reference_head + 1U) % AEC_REFERENCE_FRAMES;
        --s_aec_reference_count;
        ++s_aec_reference_overruns;
    }
    size_t tail = (s_aec_reference_head + s_aec_reference_count) %
                  AEC_REFERENCE_FRAMES;
    memcpy(s_aec_reference_ring + tail * s_aec_frame_samples, frame,
           s_aec_frame_samples * sizeof(*frame));
    ++s_aec_reference_count;
}

static void aec_reference_push(const int16_t *pcm, size_t samples)
{
    if (s_aec_reference_ring == NULL || s_aec_frame_samples == 0U) return;
    rtos_lock_mutex(&s_ring_mutex);
    while (samples != 0U) {
        size_t copy_samples = s_aec_frame_samples -
                              s_aec_reference_partial_count;
        if (copy_samples > samples) copy_samples = samples;
        memcpy(s_aec_reference_partial + s_aec_reference_partial_count, pcm,
               copy_samples * sizeof(*pcm));
        s_aec_reference_partial_count += copy_samples;
        pcm += copy_samples;
        samples -= copy_samples;
        if (s_aec_reference_partial_count == s_aec_frame_samples) {
            aec_reference_store_frame_locked(s_aec_reference_partial);
            s_aec_reference_partial_count = 0U;
        }
    }
    rtos_unlock_mutex(&s_ring_mutex);
}

static bool aec_reference_take(int16_t *frame)
{
    bool available = false;
    rtos_lock_mutex(&s_ring_mutex);
    if (s_aec_reference_count != 0U) {
        memcpy(frame,
               s_aec_reference_ring +
                   s_aec_reference_head * s_aec_frame_samples,
               s_aec_frame_samples * sizeof(*frame));
        s_aec_reference_head =
            (s_aec_reference_head + 1U) % AEC_REFERENCE_FRAMES;
        --s_aec_reference_count;
        available = true;
    }
    rtos_unlock_mutex(&s_ring_mutex);
    return available;
}

static uint32_t pcm_abs(int16_t sample)
{
    return sample == INT16_MIN ? 32768U :
           (uint32_t)(sample < 0 ? -sample : sample);
}

static size_t aec_reference_depth(void)
{
    size_t depth;
    rtos_lock_mutex(&s_ring_mutex);
    depth = s_aec_reference_count;
    rtos_unlock_mutex(&s_ring_mutex);
    return depth;
}

static void echo_diag_accumulate(const int16_t *mic, const int16_t *reference,
                                 const int16_t *output, size_t samples)
{
    for (size_t i = 0U; i < samples; ++i) {
        uint32_t mic_abs = pcm_abs(mic[i]);
        uint32_t ref_abs = pcm_abs(reference[i]);
        uint32_t out_abs = pcm_abs(output[i]);
        s_echo_diag_mic_abs_sum += mic_abs;
        s_echo_diag_ref_abs_sum += ref_abs;
        s_echo_diag_out_abs_sum += out_abs;
        if (mic_abs > s_echo_diag_mic_peak) s_echo_diag_mic_peak = mic_abs;
        if (ref_abs > s_echo_diag_ref_peak) s_echo_diag_ref_peak = ref_abs;
        if (out_abs > s_echo_diag_out_peak) s_echo_diag_out_peak = out_abs;
    }
    s_echo_diag_samples += (uint32_t)samples;
    ++s_echo_diag_frames;
    if (s_echo_diag_frames < ECHO_DIAG_INTERVAL_FRAMES) return;

    uint32_t mic_mean = s_echo_diag_samples == 0U ? 0U :
        (uint32_t)(s_echo_diag_mic_abs_sum / s_echo_diag_samples);
    uint32_t ref_mean = s_echo_diag_samples == 0U ? 0U :
        (uint32_t)(s_echo_diag_ref_abs_sum / s_echo_diag_samples);
    uint32_t out_mean = s_echo_diag_samples == 0U ? 0U :
        (uint32_t)(s_echo_diag_out_abs_sum / s_echo_diag_samples);
    uint32_t residual_percent = mic_mean == 0U ? 0U :
        (uint32_t)(((uint64_t)out_mean * 100U) / mic_mean);
    uint32_t underruns = s_aec_reference_underruns -
                          s_echo_diag_last_underruns;
    uint32_t overruns = s_aec_reference_overruns -
                         s_echo_diag_last_overruns;
    BK_LOGD(TAG,
            "ECHO_DIAG AEC codec=%s frames=%u mic-abs=%u ref-abs=%u "
            "out-abs=%u residual=%u%% peaks=%u/%u/%u ref-depth=%u "
            "ref-under=%u ref-over=%u\n",
            s_codec == XIAOTAI_AUDIO_CODEC_OPUS_16K ? "opus" : "alaw",
            (unsigned)s_echo_diag_frames, (unsigned)mic_mean,
            (unsigned)ref_mean, (unsigned)out_mean,
            (unsigned)residual_percent, (unsigned)s_echo_diag_mic_peak,
            (unsigned)s_echo_diag_ref_peak, (unsigned)s_echo_diag_out_peak,
            (unsigned)aec_reference_depth(), (unsigned)underruns,
            (unsigned)overruns);

    s_echo_diag_mic_abs_sum = 0U;
    s_echo_diag_ref_abs_sum = 0U;
    s_echo_diag_out_abs_sum = 0U;
    s_echo_diag_samples = 0U;
    s_echo_diag_frames = 0U;
    s_echo_diag_mic_peak = 0U;
    s_echo_diag_ref_peak = 0U;
    s_echo_diag_out_peak = 0U;
    s_echo_diag_last_underruns = s_aec_reference_underruns;
    s_echo_diag_last_overruns = s_aec_reference_overruns;
}

static void aec_process_capture(int16_t *pcm, size_t samples)
{
    if (s_aec == NULL || s_aec_frame_samples == 0U) return;
    for (size_t offset = 0U;
         offset + s_aec_frame_samples <= samples;
         offset += s_aec_frame_samples) {
        if (!aec_reference_take(s_aec_ref)) {
            memset(s_aec_ref, 0,
                   s_aec_frame_samples * sizeof(*s_aec_ref));
            ++s_aec_reference_underruns;
        }
        memcpy(s_aec_mic, pcm + offset,
               s_aec_frame_samples * sizeof(*s_aec_mic));
        if (xiaotai_audio_aec_reference_active(s_aec_ref,
                                               s_aec_frame_samples)) {
            aec_proc(s_aec, s_aec_ref, s_aec_mic, s_aec_out);
        } else {
            /* Running the BK AEC against a missing or effectively silent
             * playback reference attenuates near-end speech.  Preserve the
             * microphone until an audible DAC reference is available. */
            memcpy(s_aec_out, s_aec_mic,
                   s_aec_frame_samples * sizeof(*s_aec_out));
        }
        echo_diag_accumulate(s_aec_mic, s_aec_ref, s_aec_out,
                             s_aec_frame_samples);
        memcpy(pcm + offset, s_aec_out,
               s_aec_frame_samples * sizeof(*pcm));
        ++s_aec_processed_frames;
    }
}

static size_t opus_ring_take(uint8_t *output, size_t capacity)
{
    if (!s_ring_mutex_ready || rtos_trylock_mutex(&s_ring_mutex) != BK_OK) {
        return 0U;
    }
    if (s_opus_count == 0U) {
        rtos_unlock_mutex(&s_ring_mutex);
        return 0U;
    }
    size_t size = s_opus_lengths[s_opus_head];
    if (size > capacity) size = capacity;
    memcpy(output, s_opus_ring + s_opus_head * OPUS_MAX_PACKET_BYTES, size);
    s_opus_head = (s_opus_head + 1U) % OPUS_PACKET_SLOTS;
    --s_opus_count;
    /* Dequeue transfers ownership to the decoder, before write_pcm begins. */
    atomic_store_explicit(&s_playback_in_progress, true, memory_order_release);
    rtos_unlock_mutex(&s_ring_mutex);
    return size;
}

static size_t ring_take(uint8_t *output, size_t size)
{
    if (!s_ring_mutex_ready || rtos_trylock_mutex(&s_ring_mutex) != BK_OK) {
        return 0;
    }
    if (s_ring_count < size) {
        rtos_unlock_mutex(&s_ring_mutex);
        return 0;
    }
    size_t first = AUDIO_RING_BYTES - s_ring_head;
    if (first > size) first = size;
    memcpy(output, &s_alaw_ring[s_ring_head], first);
    if (first < size) memcpy(output + first, s_alaw_ring, size - first);
    s_ring_head = (s_ring_head + size) % AUDIO_RING_BYTES;
    s_ring_count -= size;
    rtos_unlock_mutex(&s_ring_mutex);
    return size;
}

static void capture_task(beken_thread_arg_t argument)
{
    (void)argument;
    int16_t pcm[AUDIO_MAX_FRAME_SAMPLES];
    uint8_t alaw[AUDIO_ALAW_FRAME_BYTES];
    uint8_t opus[OPUS_MAX_PACKET_BYTES];
    int16_t narrow_pcm[AUDIO_ALAW_FRAME_BYTES];

    BK_LOGI(TAG, "capture task started: uplink=%s rate=%u DSP-samples=%u\n",
            s_uplink_codec == XIAOTAI_AUDIO_CODEC_OPUS_16K ? "opus" : "alaw",
            s_uplink_codec == XIAOTAI_AUDIO_CODEC_OPUS_16K ? 16000U : 8000U,
            (unsigned)s_frame_samples);
    while (s_capture_run) {
        size_t bytes_read = 0U;
        int rc = s_board_audio->read_pcm(s_board_audio->context, pcm,
                                         s_frame_samples,
                                         &bytes_read);
        if (rc != BK_OK ||
            bytes_read != s_frame_samples * sizeof(*pcm)) {
            if (s_capture_run) {
                BK_LOGW(TAG, "microphone read failed rc=%d bytes=%u\n",
                        rc, (unsigned)bytes_read);
                rtos_delay_milliseconds(10);
            }
            continue;
        }
        aec_process_capture(pcm, s_frame_samples);
        bool muted = atomic_load_explicit(&s_microphone_muted,
                                          memory_order_acquire);
        xiaotai_audio_agc_stats_t agc_stats = {0};
        if (muted) {
            memset(pcm, 0, s_frame_samples * sizeof(*pcm));
        } else {
            if (s_codec == XIAOTAI_AUDIO_CODEC_OPUS_16K) {
                bool speech = false;
                if (xiaotai_audio_ns_process(&s_uplink_ns, pcm,
                                             s_frame_samples, &speech)) {
                    if (speech) {
                        ++s_uplink_ns_speech_frames;
                    } else {
                        ++s_uplink_ns_noise_frames;
                    }
                } else {
                    ++s_uplink_ns_failures;
                    if (s_uplink_ns_failures == 1U ||
                        (s_uplink_ns_failures & 0x7fU) == 0U) {
                        BK_LOGW(TAG,
                                "SDK NS process failed count=%u; using AEC PCM\n",
                                (unsigned)s_uplink_ns_failures);
                    }
                }
            }
            if (!xiaotai_audio_agc_process(&s_uplink_agc, pcm,
                                            s_frame_samples,
                                            &agc_stats)) {
                ++s_uplink_agc_failures;
                if (s_uplink_agc_failures == 1U ||
                    (s_uplink_agc_failures & 0x7fU) == 0U) {
                    BK_LOGW(TAG,
                            "SDK AGC process failed count=%u; using filtered PCM\n",
                            (unsigned)s_uplink_agc_failures);
                }
            }
        }
        ++s_uplink_level_frames;
        if (s_uplink_level_frames == 1U ||
            (s_uplink_level_frames %
             UPLINK_LEVEL_LOG_INTERVAL_FRAMES) == 0U) {
            BK_LOGD(TAG,
                    "SDK AGC level in=%u out=%u limited=%u failures=%u\n",
                    (unsigned)agc_stats.input_peak,
                    (unsigned)agc_stats.output_peak,
                    (unsigned)agc_stats.limited_samples,
                    (unsigned)s_uplink_agc_failures);
            if (s_codec == XIAOTAI_AUDIO_CODEC_OPUS_16K) {
                BK_LOGD(TAG,
                        "SDK NS frames speech=%u noise=%u failures=%u suppression=%ddB\n",
                        (unsigned)s_uplink_ns_speech_frames,
                        (unsigned)s_uplink_ns_noise_frames,
                        (unsigned)s_uplink_ns_failures,
                        AI_NS_SUPPRESSION_DB);
            }
        }
        const uint8_t *encoded = alaw;
        size_t encoded_size = sizeof(alaw);
        if (s_uplink_codec == XIAOTAI_AUDIO_CODEC_OPUS_16K) {
            int length = opus_encode(s_opus_encoder, pcm, OPUS_FRAME_SAMPLES,
                                     opus, sizeof(opus));
            if (length < 0) {
                BK_LOGW(TAG, "Opus encode failed rc=%d\n", length);
                continue;
            }
            encoded = opus;
            encoded_size = (size_t)length;
        } else {
            const int16_t *wire_pcm = pcm;
            size_t wire_samples = s_frame_samples;
            if (s_codec == XIAOTAI_AUDIO_CODEC_OPUS_16K) {
                wire_samples = starter_audio_resampler_16k_to_8k_process(
                    &s_uplink_resampler, pcm, s_frame_samples,
                    narrow_pcm, AUDIO_ALAW_FRAME_BYTES);
                wire_pcm = narrow_pcm;
            }
            if (wire_samples != AUDIO_ALAW_FRAME_BYTES) continue;
            for (size_t i = 0; i < wire_samples; ++i) {
                alaw[i] = muted ? 0xd5U : linear2alaw((int)wire_pcm[i]);
            }
            encoded_size = wire_samples;
        }
        xiaotai_audio_uplink_fn callback = s_uplink;
        if (callback != NULL &&
            atomic_load_explicit(&s_uplink_enabled, memory_order_acquire)) {
            int rc = callback(encoded, encoded_size, rtos_get_time(),
                              s_uplink_context);
            if (rc >= 0) {
                ++s_uplink_frames;
                if (s_uplink_frames == 1U) {
                    BK_LOGI(TAG, "first uplink frame sent bytes=%u ts=%u\n",
                            (unsigned)encoded_size, (unsigned)rtos_get_time());
                }
            } else {
                ++s_uplink_rejected;
                if (s_uplink_rejected == 1U ||
                    (s_uplink_rejected & 0x7fU) == 0U) {
                    BK_LOGW(TAG, "uplink not ready rc=%d count=%u\n", rc,
                            (unsigned)s_uplink_rejected);
                }
            }
        }
    }
    s_capture_thread = NULL;
    rtos_set_semaphore(&s_capture_stopped);
    rtos_delete_thread(NULL);
}

static void playback_task(beken_thread_arg_t argument)
{
    (void)argument;
    uint8_t alaw[AUDIO_ALAW_FRAME_BYTES];
    uint8_t opus[OPUS_MAX_PACKET_BYTES];
    int16_t pcm[OPUS_MAX_DECODE_SAMPLES];

    BK_LOGI(TAG, "playback task started: codec=%s jitter=%u ms\n",
            s_codec == XIAOTAI_AUDIO_CODEC_OPUS_16K ? "opus" : "alaw",
            (unsigned)(s_codec == XIAOTAI_AUDIO_CODEC_OPUS_16K ?
                OPUS_PACKET_SLOTS * 20U : AUDIO_RING_FRAMES * 20U));
    while (s_play_run) {
        bool media_frame = true;
        size_t samples = s_frame_samples;
        size_t encoded_size = sizeof(alaw);
        if (s_codec == XIAOTAI_AUDIO_CODEC_OPUS_16K) {
            encoded_size = opus_ring_take(opus, sizeof(opus));
            if (encoded_size == 0U) {
                if (atomic_load(&s_call_ring_kind) == XIAOTAI_CALL_RING_NONE) {
                    rtos_delay_milliseconds(5);
                    continue;
                }
                media_frame = false;
                memset(pcm, 0, samples * sizeof(*pcm));
            }
            int decoded = media_frame ? opus_decode(s_opus_decoder, opus,
                                      (opus_int32)encoded_size, pcm,
                                      OPUS_MAX_DECODE_SAMPLES, 0) : (int)samples;
            if (decoded <= 0) {
                atomic_store_explicit(&s_playback_in_progress, false,
                                      memory_order_release);
                BK_LOGW(TAG, "Opus decode failed rc=%d bytes=%u\n", decoded,
                        (unsigned)encoded_size);
                continue;
            }
            samples = (size_t)decoded;
        } else {
            if (ring_take(alaw, sizeof(alaw)) != sizeof(alaw)) {
                if (atomic_load(&s_call_ring_kind) == XIAOTAI_CALL_RING_NONE) {
                    rtos_delay_milliseconds(5);
                    continue;
                }
                media_frame = false;
                memset(pcm, 0, samples * sizeof(*pcm));
            }
        }
        for (size_t i = 0; i < samples; ++i) {
            if (s_codec != XIAOTAI_AUDIO_CODEC_OPUS_16K && media_frame)
                pcm[i] = alaw2linear(alaw[i]);
        }
        mix_call_ringtone(pcm, samples,
            s_codec == XIAOTAI_AUDIO_CODEC_OPUS_16K ? 16000U : 8000U);
        unsigned volume = atomic_load_explicit(&s_volume, memory_order_acquire);
        bool muted = atomic_load_explicit(&s_speaker_muted,
                                          memory_order_acquire);
        for (size_t i = 0; i < samples; ++i) {
            int32_t sample = pcm[i];
            pcm[i] = muted ? 0 :
                (int16_t)((sample * (int32_t)volume) / 10);
        }
        size_t bytes_written = 0U;
        atomic_store_explicit(&s_playback_in_progress, true,
                              memory_order_release);
        int rc = s_board_audio->write_pcm(s_board_audio->context, pcm,
                                          samples,
                                          &bytes_written);
        if (rc != BK_OK || bytes_written != samples * sizeof(*pcm)) {
            BK_LOGW(TAG, "speaker write failed rc=%d\n", rc);
        } else if (media_frame) {
            if (s_codec == XIAOTAI_AUDIO_CODEC_OPUS_16K) {
                atomic_store_explicit(&s_playback_columns,
                    playback_columns_pack(pcm, samples), memory_order_relaxed);
                atomic_store_explicit(&s_playback_columns_at_ms,
                    rtos_get_time(), memory_order_release);
            }
            /* write_pcm may enqueue into the board DMA path.  Measure the
             * quiet tail from the last successful write, not only from
             * network receipt, so teardown cannot clip that final frame. */
            atomic_store_explicit(&s_last_downlink_ms, rtos_get_time(),
                                  memory_order_release);
            uint32_t now_ms = rtos_get_time();
            uint32_t sample_rate_hz =
                s_codec == XIAOTAI_AUDIO_CODEC_OPUS_16K ? 16000U : 8000U;
            uint32_t frame_duration_ms =
                (uint32_t)((samples * 1000U) / sample_rate_hz);
            atomic_fetch_add_explicit(&s_playback_pcm_ms, frame_duration_ms,
                                      memory_order_relaxed);
            uint32_t tail_ms = atomic_load_explicit(
                &s_playback_tail_ms, memory_order_acquire);
            atomic_store_explicit(
                &s_playback_tail_ms,
                xiaotai_audio_playback_tail_after_write(
                    tail_ms, now_ms, frame_duration_ms),
                memory_order_release);
            aec_reference_push(pcm, samples);
            ++s_downlink_frames;
            if (s_downlink_frames == 1U) {
                BK_LOGI(TAG, "first downlink frame played bytes=%u\n",
                        (unsigned)(samples * sizeof(*pcm)));
            }
        }
        if (!media_frame && rc == BK_OK && bytes_written == samples * sizeof(*pcm))
            aec_reference_push(pcm, samples);
        atomic_store_explicit(&s_playback_in_progress, false,
                              memory_order_release);
    }
    s_play_thread = NULL;
    rtos_set_semaphore(&s_play_stopped);
    rtos_delete_thread(NULL);
}

static void destroy_devices(void)
{
    if (s_board_audio != NULL && s_board_audio->stop != NULL) {
        (void)s_board_audio->stop(s_board_audio->context);
    }
}

int xiaotai_audio_start(xiaotai_audio_codec_t codec,
                        xiaotai_audio_uplink_fn uplink, void *context)
{
    return xiaotai_audio_start_duplex(codec, codec, uplink, context);
}

static int audio_start_duplex_locked(xiaotai_audio_codec_t uplink_codec,
                               xiaotai_audio_codec_t codec,
                               xiaotai_audio_uplink_fn uplink, void *context)
{
    if (uplink == NULL ||
        (uplink_codec != XIAOTAI_AUDIO_CODEC_ALAW_8K &&
         uplink_codec != XIAOTAI_AUDIO_CODEC_OPUS_16K) ||
        (uplink_codec == XIAOTAI_AUDIO_CODEC_OPUS_16K &&
         codec != XIAOTAI_AUDIO_CODEC_OPUS_16K)) return BK_ERR_PARAM;
    if (codec != XIAOTAI_AUDIO_CODEC_ALAW_8K &&
        codec != XIAOTAI_AUDIO_CODEC_OPUS_16K) return BK_ERR_PARAM;
    if (s_running) return BK_ERR_BUSY;
    /* Starting call media is the accept boundary, unlike receipt of WHIP parameters. */
    int outgoing_ring = XIAOTAI_CALL_RING_OUTGOING;
    if (atomic_compare_exchange_strong(&s_call_ring_kind, &outgoing_ring,
                                       XIAOTAI_CALL_RING_NONE))
        BK_LOGI(TAG, "call ringtone outgoing stopped for call media\n");
    cancel_prompt_locked();
    if (atomic_load(&s_prompt_active)) return BK_ERR_BUSY;

    if (!s_ring_mutex_ready) {
        int rc = rtos_init_mutex(&s_ring_mutex);
        if (rc != BK_OK) return rc;
        s_ring_mutex_ready = true;
    }
    int rc = rtos_init_semaphore(&s_capture_stopped, 1);
    if (rc != BK_OK) return rc;
    rc = rtos_init_semaphore(&s_play_stopped, 1);
    if (rc != BK_OK) {
        rtos_deinit_semaphore(&s_capture_stopped);
        return rc;
    }

    s_board_audio = xiaotai_board_audio_adapter();
    if (s_board_audio == NULL || s_board_audio->start == NULL ||
        s_board_audio->request_stop == NULL || s_board_audio->stop == NULL ||
        s_board_audio->format == NULL || s_board_audio->read_pcm == NULL ||
        s_board_audio->write_pcm == NULL) {
        rc = BK_ERR_NOT_INIT;
        goto fail;
    }
    xiaotai_board_audio_format_t format =
        s_board_audio->format(s_board_audio->context);
    uint32_t required_rate = codec == XIAOTAI_AUDIO_CODEC_OPUS_16K ?
                             OPUS_SAMPLE_RATE_HZ : 8000U;
    size_t frame_samples =
        xiaotai_audio_frame_samples_for_rate(required_rate);
    if (frame_samples == 0U) {
        rc = BK_ERR_PARAM;
        goto fail;
    }
    if (s_board_audio->configure != NULL) {
        rc = s_board_audio->configure(s_board_audio->context, required_rate,
                                      frame_samples);
        if (rc != BK_OK) goto fail;
        format = s_board_audio->format(s_board_audio->context);
    }
    if (!xiaotai_board_audio_format_is_valid(&format) ||
        format.sample_rate_hz != required_rate ||
        format.capture_layout != XIAOTAI_CAPTURE_MONO_MIC ||
        format.playback_channels != 1U) {
        BK_LOGE(TAG, "unsupported board audio format rate=%u capture=%u playback=%u layout=%d\n",
                (unsigned)format.sample_rate_hz,
                (unsigned)format.capture_channels,
                (unsigned)format.playback_channels,
                (int)format.capture_layout);
        rc = BK_ERR_PARAM;
        goto fail;
    }
    s_codec = codec;
    s_uplink_codec = uplink_codec;
    starter_audio_resampler_16k_to_8k_reset(&s_uplink_resampler);
    s_frame_samples = frame_samples;
    atomic_store(&s_playback_columns, 0U);
    atomic_store(&s_playback_columns_at_ms, 0U);
    if (codec == XIAOTAI_AUDIO_CODEC_OPUS_16K) {
        int opus_error = OPUS_OK;
        s_opus_ring = psram_malloc(OPUS_PACKET_SLOTS * OPUS_MAX_PACKET_BYTES);
        if (s_opus_ring == NULL) {
            rc = BK_ERR_NO_MEM;
            goto fail;
        }
        s_opus_encoder = opus_encoder_create(OPUS_SAMPLE_RATE_HZ, 1,
                                              OPUS_APPLICATION_VOIP,
                                              &opus_error);
        if (s_opus_encoder == NULL || opus_error != OPUS_OK) {
            BK_LOGE(TAG, "Opus encoder create failed rc=%d\n", opus_error);
            rc = BK_ERR_NO_MEM;
            goto fail;
        }
        s_opus_decoder = opus_decoder_create(OPUS_SAMPLE_RATE_HZ, 1,
                                              &opus_error);
        if (s_opus_decoder == NULL || opus_error != OPUS_OK ||
            opus_encoder_ctl(s_opus_encoder,
                             OPUS_SET_BITRATE(OPUS_BITRATE_BPS)) != OPUS_OK ||
            opus_encoder_ctl(s_opus_encoder,
                             OPUS_SET_COMPLEXITY(3)) != OPUS_OK ||
            opus_encoder_ctl(s_opus_encoder,
                             OPUS_SET_SIGNAL(OPUS_SIGNAL_VOICE)) != OPUS_OK) {
            BK_LOGE(TAG, "Opus codec configure failed rc=%d\n", opus_error);
            rc = BK_FAIL;
            goto fail;
        }
        BK_LOGI(TAG,
                "Opus ready rate=16000 frame=20ms bitrate=16000 state=%d+%d heap=%u\n",
                opus_encoder_get_size(1), opus_decoder_get_size(1),
                (unsigned)rtos_get_free_heap_size());
        if (!xiaotai_audio_ns_start(&s_uplink_ns, required_rate,
                                    frame_samples,
                                    AI_NS_SUPPRESSION_DB)) {
            BK_LOGE(TAG,
                    "BK SDK NS initialization failed rate=%u frame=%u suppression=%ddB\n",
                    (unsigned)required_rate, (unsigned)frame_samples,
                    AI_NS_SUPPRESSION_DB);
            rc = BK_FAIL;
            goto fail;
        }
        BK_LOGI(TAG,
                "BK SDK NS ready rate=16000 frame=320 suppression=-25dB\n");
    }
    uint32_t heap_before = rtos_get_free_heap_size();
    if (!xiaotai_audio_agc_start(&s_uplink_agc, required_rate,
                                 frame_samples)) {
        BK_LOGE(TAG, "BK SDK AGC initialization failed rate=%u frame=%u\n",
                (unsigned)required_rate, (unsigned)frame_samples);
        rc = BK_FAIL;
        goto fail;
    }
    BK_LOGI(TAG,
            "BK SDK AGC ready rate=%u frame=%u target=-3dBFS compression=16dB limiter=1 heap=%u->%u\n",
            (unsigned)required_rate, (unsigned)frame_samples,
            (unsigned)heap_before,
            (unsigned)rtos_get_free_heap_size());
    rc = s_board_audio->start(s_board_audio->context);
    if (rc == BK_OK && s_board_audio->set_muted != NULL) {
        rc = s_board_audio->set_muted(
            s_board_audio->context,
            atomic_load_explicit(&s_speaker_muted, memory_order_acquire));
    }
    if (rc != BK_OK) goto fail;

    if (xiaotai_audio_aec_profile_validated(required_rate)) {
        rc = aec_start(required_rate);
        if (rc != BK_OK) goto fail;
    } else {
        BK_LOGI(TAG,
                "AEC bypass codec=alaw reason=8k-profile-unvalidated\n");
    }

    ring_reset();
    s_dropped_bytes = 0;
    s_drop_reported_bytes = 0;
    s_uplink_frames = 0;
    s_uplink_rejected = 0;
    s_uplink_level_frames = 0;
    s_uplink_agc_failures = 0;
    s_uplink_ns_speech_frames = 0;
    s_uplink_ns_noise_frames = 0;
    s_uplink_ns_failures = 0;
    s_downlink_frames = 0;
    s_opus_queue_peak = 0U;
    atomic_store(&s_opus_received_frames, 0U);
    atomic_store(&s_admission_busy, 0U);
    atomic_store(&s_playback_pcm_ms, 0U);
    atomic_store(&s_last_opus_rx_ms, rtos_get_time());
    atomic_store_explicit(&s_playback_in_progress, false,
                          memory_order_release);
    atomic_store_explicit(&s_last_downlink_ms, rtos_get_time(),
                          memory_order_release);
    atomic_store_explicit(&s_playback_tail_ms, rtos_get_time(),
                          memory_order_release);
    s_aec_processed_frames = 0;
    s_aec_reference_underruns = 0;
    s_aec_reference_overruns = 0;
    s_echo_diag_mic_abs_sum = 0U;
    s_echo_diag_ref_abs_sum = 0U;
    s_echo_diag_out_abs_sum = 0U;
    s_echo_diag_samples = 0U;
    s_echo_diag_frames = 0U;
    s_echo_diag_mic_peak = 0U;
    s_echo_diag_ref_peak = 0U;
    s_echo_diag_out_peak = 0U;
    s_echo_diag_last_underruns = 0U;
    s_echo_diag_last_overruns = 0U;
    s_uplink = uplink;
    s_uplink_context = context;
    s_capture_joined = false;
    s_play_joined = false;
    s_play_run = true;
    uint32_t playback_stack = codec == XIAOTAI_AUDIO_CODEC_OPUS_16K ?
                              OPUS_PLAYBACK_TASK_STACK : AUDIO_TASK_STACK;
    uint32_t capture_stack = codec == XIAOTAI_AUDIO_CODEC_OPUS_16K ?
                             OPUS_CAPTURE_TASK_STACK :
                             AEC_CAPTURE_TASK_STACK;
    rc = rtos_create_psram_thread(&s_play_thread, AUDIO_TASK_PRIORITY,
                                  "xiaotai_spk", playback_task,
                                  playback_stack, NULL);
    if (rc != BK_OK) goto fail_started;

    s_capture_run = true;
    rc = rtos_create_psram_thread(&s_capture_thread, AUDIO_TASK_PRIORITY,
                                  "xiaotai_mic", capture_task,
                                  capture_stack, NULL);
    if (rc != BK_OK) goto fail_started;

    s_running = true;
    BK_LOGI(TAG, "full-duplex board audio started uplink=%s/%u downlink=%s/%u DSP=%u\n",
            uplink_codec == XIAOTAI_AUDIO_CODEC_OPUS_16K ? "opus" : "alaw",
            uplink_codec == XIAOTAI_AUDIO_CODEC_OPUS_16K ? 16000U : 8000U,
            codec == XIAOTAI_AUDIO_CODEC_OPUS_16K ? "opus" : "alaw",
            codec == XIAOTAI_AUDIO_CODEC_OPUS_16K ? 16000U : 8000U,
            (unsigned)required_rate);
    return BK_OK;

fail_started:
    s_capture_run = false;
    s_play_run = false;
    if (s_board_audio != NULL && s_board_audio->request_stop != NULL) {
        (void)s_board_audio->request_stop(s_board_audio->context);
    }
    if (s_capture_thread != NULL) {
        rtos_get_semaphore(&s_capture_stopped, AUDIO_TASK_STOP_TIMEOUT_MS);
    }
    if (s_play_thread != NULL) {
        rtos_get_semaphore(&s_play_stopped, AUDIO_TASK_STOP_TIMEOUT_MS);
    }
fail:
    s_uplink = NULL;
    s_uplink_context = NULL;
    xiaotai_audio_agc_stop(&s_uplink_agc);
    xiaotai_audio_ns_stop(&s_uplink_ns);
    destroy_devices();
    if (s_opus_encoder != NULL) {
        opus_encoder_destroy(s_opus_encoder);
        s_opus_encoder = NULL;
    }
    if (s_opus_decoder != NULL) {
        opus_decoder_destroy(s_opus_decoder);
        s_opus_decoder = NULL;
    }
    if (s_opus_ring != NULL) {
        psram_free(s_opus_ring);
        s_opus_ring = NULL;
    }
    aec_destroy();
    rtos_deinit_semaphore(&s_capture_stopped);
    rtos_deinit_semaphore(&s_play_stopped);
    return rc;
}

int xiaotai_audio_start_duplex(xiaotai_audio_codec_t uplink_codec,
                               xiaotai_audio_codec_t codec,
                               xiaotai_audio_uplink_fn uplink, void *context)
{
    if (!audio_control_enter(true)) return BK_ERR_BUSY;
    int rc = audio_start_duplex_locked(uplink_codec, codec, uplink, context);
    audio_control_leave();
    return rc;
}

int xiaotai_audio_play_alaw(const uint8_t *data, size_t size)
{
    if (data == NULL || size == 0) return BK_ERR_PARAM;
    if (!s_running || !s_play_run ||
        s_codec != XIAOTAI_AUDIO_CODEC_ALAW_8K) return BK_ERR_NOT_INIT;
    /* The pinned SMP SDK takes ticks here despite the timeout_ms parameter
     * name. Allow one scheduler tick for a short ring/AEC copy to finish;
     * never wait indefinitely inside the TiRTC receive callback. */
    if (rtos_trylock_mutex(&s_ring_mutex) != BK_OK &&
        rtos_lock_mutex_timeout(&s_ring_mutex, 1U) != BK_OK) {
        atomic_fetch_add_explicit(&s_admission_busy, 1U, memory_order_relaxed);
        return BK_ERR_BUSY;
    }
    if (!s_running || !s_play_run) {
        rtos_unlock_mutex(&s_ring_mutex);
        return BK_ERR_NOT_INIT;
    }

    if (size > AUDIO_RING_BYTES) {
        data += size - AUDIO_RING_BYTES;
        s_dropped_bytes += (uint32_t)(size - AUDIO_RING_BYTES);
        size = AUDIO_RING_BYTES;
    }
    size_t free_bytes = AUDIO_RING_BYTES - s_ring_count;
    if (size > free_bytes) {
        size_t discard = size - free_bytes;
        s_ring_head = (s_ring_head + discard) % AUDIO_RING_BYTES;
        s_ring_count -= discard;
        s_dropped_bytes += (uint32_t)discard;
    }
    size_t tail = (s_ring_head + s_ring_count) % AUDIO_RING_BYTES;
    size_t first = AUDIO_RING_BYTES - tail;
    if (first > size) first = size;
    memcpy(&s_alaw_ring[tail], data, first);
    if (first < size) memcpy(s_alaw_ring, data + first, size - first);
    s_ring_count += size;
    atomic_store_explicit(&s_last_downlink_ms, rtos_get_time(),
                          memory_order_release);
    uint32_t dropped = s_dropped_bytes;
    bool report_drop = xiaotai_audio_drop_report_due(
        dropped, &s_drop_reported_bytes);
    rtos_unlock_mutex(&s_ring_mutex);

    if (report_drop) {
        BK_LOGW(TAG,
                "downlink jitter exceeded %u ms; cumulative dropped=%u bytes\n",
                (unsigned)(AUDIO_RING_FRAMES * 20U),
                (unsigned)dropped);
    }
    return (int)size;
}

int xiaotai_audio_play_opus(const uint8_t *data, size_t size)
{
    if (data == NULL || size == 0U || size > OPUS_MAX_PACKET_BYTES) {
        return BK_ERR_PARAM;
    }
    if (!s_running || !s_play_run ||
        s_codec != XIAOTAI_AUDIO_CODEC_OPUS_16K) return BK_ERR_NOT_INIT;
    /* The pinned SMP SDK takes ticks here despite the timeout_ms parameter
     * name. Allow one scheduler tick for a short ring/AEC copy to finish;
     * never wait indefinitely inside the TiRTC receive callback. */
    if (rtos_trylock_mutex(&s_ring_mutex) != BK_OK &&
        rtos_lock_mutex_timeout(&s_ring_mutex, 1U) != BK_OK) {
        atomic_fetch_add_explicit(&s_admission_busy, 1U, memory_order_relaxed);
        return BK_ERR_BUSY;
    }
    if (!s_running || !s_play_run) {
        rtos_unlock_mutex(&s_ring_mutex);
        return BK_ERR_NOT_INIT;
    }
    if (s_opus_count == OPUS_PACKET_SLOTS) {
        s_opus_head = (s_opus_head + 1U) % OPUS_PACKET_SLOTS;
        --s_opus_count;
        ++s_dropped_bytes;
    }
    size_t tail = (s_opus_head + s_opus_count) % OPUS_PACKET_SLOTS;
    memcpy(s_opus_ring + tail * OPUS_MAX_PACKET_BYTES, data, size);
    s_opus_lengths[tail] = (uint16_t)size;
    ++s_opus_count;
    if (s_opus_count > s_opus_queue_peak) s_opus_queue_peak = s_opus_count;
    atomic_fetch_add_explicit(&s_opus_received_frames, 1, memory_order_relaxed);
    atomic_store_explicit(&s_last_opus_rx_ms, rtos_get_time(), memory_order_release);
    atomic_store_explicit(&s_last_downlink_ms, rtos_get_time(),
                          memory_order_release);
    rtos_unlock_mutex(&s_ring_mutex);
    return (int)size;
}

static int audio_stop_locked(void)
{
    if (!s_running) return BK_OK;
    s_running = false;
    s_uplink = NULL;
    s_uplink_context = NULL;
    s_capture_run = false;
    s_play_run = false;

    if (s_board_audio != NULL && s_board_audio->request_stop != NULL) {
        (void)s_board_audio->request_stop(s_board_audio->context);
    }
    /* Completion signals are one-shot. Retain each successful join when the
     * other worker times out so a retry only waits for unfinished workers. */
    int capture_rc = BK_OK;
    int play_rc = BK_OK;
    if (!s_capture_joined) {
        capture_rc = rtos_get_semaphore(&s_capture_stopped,
                                        AUDIO_TASK_STOP_TIMEOUT_MS);
        s_capture_joined = capture_rc == BK_OK;
    }
    if (!s_play_joined) {
        play_rc = rtos_get_semaphore(&s_play_stopped,
                                     AUDIO_TASK_STOP_TIMEOUT_MS);
        s_play_joined = play_rc == BK_OK;
    }
    if (capture_rc != BK_OK || play_rc != BK_OK) {
        BK_LOGE(TAG, "audio task stop timed out capture=%d play=%d\n",
                capture_rc, play_rc);
        /* Keep the instance owned so a later start cannot reinitialize live
         * synchronization objects or destroy devices still used by a task. */
        s_running = true;
        return BK_FAIL;
    }

    destroy_devices();
    if (s_opus_encoder != NULL) {
        opus_encoder_destroy(s_opus_encoder);
        s_opus_encoder = NULL;
    }
    if (s_opus_decoder != NULL) {
        opus_decoder_destroy(s_opus_decoder);
        s_opus_decoder = NULL;
    }
    if (s_opus_ring != NULL) {
        psram_free(s_opus_ring);
        s_opus_ring = NULL;
    }
    aec_destroy();
    xiaotai_audio_ns_stop(&s_uplink_ns);
    xiaotai_audio_agc_stop(&s_uplink_agc);
    ring_reset();
    rtos_deinit_semaphore(&s_capture_stopped);
    rtos_deinit_semaphore(&s_play_stopped);
    BK_LOGI(TAG,
            "audio stopped up=%u down=%u dropped=%u aec=%u ref-under=%u ref-over=%u busy=%u\n",
            (unsigned)s_uplink_frames, (unsigned)s_downlink_frames,
            (unsigned)s_dropped_bytes, (unsigned)s_aec_processed_frames,
            (unsigned)s_aec_reference_underruns,
            (unsigned)s_aec_reference_overruns,
            (unsigned)atomic_load(&s_admission_busy));
    return BK_OK;
}

int xiaotai_audio_stop(void)
{
    if (!audio_control_enter(true)) return BK_ERR_BUSY;
    int rc = audio_stop_locked();
    audio_control_leave();
    return rc;
}

bool xiaotai_audio_running(void)
{
    return s_running;
}

bool xiaotai_audio_playback_is_drained(uint32_t quiet_ms)
{
    bool running = atomic_load_explicit(&s_running, memory_order_acquire);
    if (!running) return true;
    if (!s_ring_mutex_ready ||
        rtos_trylock_mutex(&s_ring_mutex) != BK_OK) {
        return false;
    }
    size_t queued = s_codec == XIAOTAI_AUDIO_CODEC_OPUS_16K ?
                    s_opus_count : s_ring_count;
    rtos_unlock_mutex(&s_ring_mutex);
    /* The worker publishes the new hardware tail before releasing ownership.
     * Snapshot in this order: C does not order function argument evaluation,
     * so inline loads could combine an old tail with a newly idle worker. */
    bool in_progress = atomic_load_explicit(&s_playback_in_progress,
                                            memory_order_acquire);
    uint32_t tail_ms = atomic_load_explicit(&s_playback_tail_ms,
                                           memory_order_acquire);
    return xiaotai_audio_playback_drained(
        running, queued, in_progress, tail_ms, rtos_get_time(), quiet_ms);
}

void xiaotai_audio_log_playback_status(const char *stage)
{
    if (!s_ring_mutex_ready ||
        rtos_trylock_mutex(&s_ring_mutex) != BK_OK) return;
    size_t queued = s_opus_count;
    size_t peak = s_opus_queue_peak;
    uint32_t dropped = s_dropped_bytes; /* Opus mode counts evicted packets. */
    rtos_unlock_mutex(&s_ring_mutex);
    uint32_t now_ms = rtos_get_time();
    int32_t tail_ms = (int32_t)(atomic_load(&s_playback_tail_ms) - now_ms);
    BK_LOGI(TAG,
            "AI playback stage=%s rx=%u written=%u overflow=%u pending=%u peak=%u slots=%u active=%d pcm-ms=%u tail-ms=%u last-rx-age-ms=%u busy=%u\n",
            stage, (unsigned)atomic_load(&s_opus_received_frames),
            (unsigned)atomic_load(&s_downlink_frames),
            (unsigned)dropped, (unsigned)queued, (unsigned)peak,
            (unsigned)OPUS_PACKET_SLOTS,
            atomic_load(&s_playback_in_progress) ? 1 : 0,
            (unsigned)atomic_load(&s_playback_pcm_ms),
            (unsigned)(tail_ms > 0 ? tail_ms : 0),
            (unsigned)(now_ms - atomic_load(&s_last_opus_rx_ms)),
            (unsigned)atomic_load(&s_admission_busy));
}

uint32_t xiaotai_audio_playback_columns(void)
{
    if (!atomic_load_explicit(&s_running, memory_order_acquire) ||
        atomic_load_explicit(&s_speaker_muted, memory_order_acquire) ||
        atomic_load_explicit(&s_volume, memory_order_acquire) == 0U)
        return 0U;
    uint32_t at_ms = atomic_load_explicit(&s_playback_columns_at_ms,
                                           memory_order_acquire);
    if (at_ms == 0U || (uint32_t)(rtos_get_time() - at_ms) > 200U)
        return 0U;
    return atomic_load_explicit(&s_playback_columns, memory_order_relaxed);
}

void xiaotai_audio_set_volume(unsigned level)
{
    if (level > 10U) level = 10U;
    atomic_store_explicit(&s_volume, level, memory_order_release);
}

unsigned xiaotai_audio_volume(void)
{
    return atomic_load_explicit(&s_volume, memory_order_acquire);
}

void xiaotai_audio_set_speaker_muted(bool muted)
{
    atomic_store_explicit(&s_speaker_muted, muted, memory_order_release);
    if (s_board_audio == NULL) s_board_audio = xiaotai_board_audio_adapter();
    if (s_board_audio != NULL && s_board_audio->set_muted != NULL) {
        (void)s_board_audio->set_muted(s_board_audio->context, muted);
    }
}

void xiaotai_audio_set_microphone_muted(bool muted)
{
    atomic_store_explicit(&s_microphone_muted, muted, memory_order_release);
}

int xiaotai_audio_set_microphone_sensitivity(unsigned sensitivity)
{
    if (sensitivity < XIAOTAI_MIC_SENSITIVITY_MIN ||
        sensitivity > XIAOTAI_MIC_SENSITIVITY_MAX) {
        return BK_ERR_PARAM;
    }
    if (s_board_audio == NULL) s_board_audio = xiaotai_board_audio_adapter();
    if (s_board_audio == NULL || s_board_audio->set_capture_gain == NULL) {
        return BK_ERR_NOT_SUPPORT;
    }
    unsigned gain = xiaotai_audio_adc_gain_for_sensitivity(sensitivity);
    int rc = s_board_audio->set_capture_gain(s_board_audio->context, gain);
    BK_LOGI(TAG, "microphone sensitivity=%u adc-gain=0x%02x rc=%d\n",
            sensitivity, gain, rc);
    return rc;
}

void xiaotai_audio_set_uplink_enabled(bool enabled)
{
    atomic_store_explicit(&s_uplink_enabled, enabled, memory_order_release);
}

bool xiaotai_audio_uplink_enabled(void)
{
    return atomic_load_explicit(&s_uplink_enabled, memory_order_acquire);
}
