#include "xiaotai_audio.h"
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
static beken_mutex_t s_ring_mutex;
static bool s_ring_mutex_ready;
static atomic_bool s_capture_run;
static atomic_bool s_play_run;
static atomic_bool s_running;
static atomic_bool s_playback_in_progress;
static atomic_uint s_last_downlink_ms;
static atomic_uint s_playback_tail_ms;
static atomic_uint s_opus_received_frames;
static atomic_uint s_playback_pcm_ms;
static atomic_uint s_last_opus_rx_ms;
static atomic_bool s_prompt_run;
static atomic_bool s_prompt_active;
static atomic_uint s_volume = 7U;
static atomic_bool s_speaker_muted;
static atomic_bool s_microphone_muted;
static atomic_bool s_uplink_enabled = true;
static xiaotai_audio_codec_t s_codec = XIAOTAI_AUDIO_CODEC_ALAW_8K;
static size_t s_frame_samples = XIAOTAI_AUDIO_ALAW_FRAME_SAMPLES;
static xiaotai_audio_agc_t s_uplink_agc;
static xiaotai_audio_ns_t s_uplink_ns;
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
    bool ready = xiaotai_board_prompt_audio_start() == BK_OK;
    if (!ready) BK_LOGE(TAG, "cannot open speaker for binding prompt\n");

    if (ready) {
        for (unsigned repeat = 0; repeat < 3U && s_prompt_run; ++repeat) {
            if (!play_prompt_pcm()) break;
            if (repeat + 1U < 3U) {
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
    atomic_store_explicit(&s_prompt_run, false, memory_order_release);
    atomic_store_explicit(&s_prompt_active, false, memory_order_release);
    s_prompt_thread = NULL;
    rtos_set_semaphore(&s_prompt_stopped);
    BK_LOGI(TAG, "binding prompt stopped\n");
    rtos_delete_thread(NULL);
}

void xiaotai_audio_cancel_prompt(void)
{
    if (!atomic_load_explicit(&s_prompt_active, memory_order_acquire)) return;
    atomic_store_explicit(&s_prompt_run, false, memory_order_release);
    if (s_prompt_sync_ready) {
        (void)rtos_get_semaphore(&s_prompt_stopped,
                                 AUDIO_TASK_STOP_TIMEOUT_MS);
    }
}

int xiaotai_audio_announce_verification_pcm(int16_t *pcm,
                                            size_t sample_count)
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
    s_prompt_pcm = pcm;
    s_prompt_samples = sample_count;
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
    BK_LOGI(TAG,
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

    BK_LOGI(TAG, "capture task started: codec=%s rate=%u mono samples=%u\n",
            s_codec == XIAOTAI_AUDIO_CODEC_OPUS_16K ? "opus" : "alaw",
            s_codec == XIAOTAI_AUDIO_CODEC_OPUS_16K ? 16000U : 8000U,
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
            BK_LOGI(TAG,
                    "SDK AGC level in=%u out=%u limited=%u failures=%u\n",
                    (unsigned)agc_stats.input_peak,
                    (unsigned)agc_stats.output_peak,
                    (unsigned)agc_stats.limited_samples,
                    (unsigned)s_uplink_agc_failures);
            if (s_codec == XIAOTAI_AUDIO_CODEC_OPUS_16K) {
                BK_LOGI(TAG,
                        "SDK NS frames speech=%u noise=%u failures=%u suppression=%ddB\n",
                        (unsigned)s_uplink_ns_speech_frames,
                        (unsigned)s_uplink_ns_noise_frames,
                        (unsigned)s_uplink_ns_failures,
                        AI_NS_SUPPRESSION_DB);
            }
        }
        const uint8_t *encoded = alaw;
        size_t encoded_size = sizeof(alaw);
        if (s_codec == XIAOTAI_AUDIO_CODEC_OPUS_16K) {
            int length = opus_encode(s_opus_encoder, pcm, OPUS_FRAME_SAMPLES,
                                     opus, sizeof(opus));
            if (length < 0) {
                BK_LOGW(TAG, "Opus encode failed rc=%d\n", length);
                continue;
            }
            encoded = opus;
            encoded_size = (size_t)length;
        } else {
            for (size_t i = 0; i < s_frame_samples; ++i) {
                alaw[i] = muted ? 0xd5U : linear2alaw((int)pcm[i]);
            }
            encoded_size = s_frame_samples;
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
        size_t samples = s_frame_samples;
        size_t encoded_size = sizeof(alaw);
        if (s_codec == XIAOTAI_AUDIO_CODEC_OPUS_16K) {
            encoded_size = opus_ring_take(opus, sizeof(opus));
            if (encoded_size == 0U) {
                rtos_delay_milliseconds(5);
                continue;
            }
            int decoded = opus_decode(s_opus_decoder, opus,
                                      (opus_int32)encoded_size, pcm,
                                      OPUS_MAX_DECODE_SAMPLES, 0);
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
                rtos_delay_milliseconds(5);
                continue;
            }
        }
        unsigned volume = atomic_load_explicit(&s_volume, memory_order_acquire);
        bool muted = atomic_load_explicit(&s_speaker_muted,
                                          memory_order_acquire);
        for (size_t i = 0; i < samples; ++i) {
            int32_t sample = s_codec == XIAOTAI_AUDIO_CODEC_OPUS_16K ?
                             pcm[i] : alaw2linear(alaw[i]);
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
        } else {
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
    if (uplink == NULL) return BK_ERR_PARAM;
    if (codec != XIAOTAI_AUDIO_CODEC_ALAW_8K &&
        codec != XIAOTAI_AUDIO_CODEC_OPUS_16K) return BK_ERR_PARAM;
    if (s_running) return BK_ERR_BUSY;
    xiaotai_audio_cancel_prompt();

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
    s_frame_samples = frame_samples;
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
    BK_LOGI(TAG, "full-duplex board audio started\n");
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

int xiaotai_audio_play_alaw(const uint8_t *data, size_t size)
{
    if (data == NULL || size == 0) return BK_ERR_PARAM;
    if (!s_running || !s_play_run ||
        s_codec != XIAOTAI_AUDIO_CODEC_ALAW_8K) return BK_ERR_NOT_INIT;
    if (rtos_trylock_mutex(&s_ring_mutex) != BK_OK) return BK_ERR_BUSY;

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
    if (rtos_trylock_mutex(&s_ring_mutex) != BK_OK) return BK_ERR_BUSY;
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

int xiaotai_audio_stop(void)
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
    /* A running instance always owns both tasks. Waiting on their completion
     * semaphores also closes the race where a task clears its handle just
     * before signalling completion. */
    int capture_rc = rtos_get_semaphore(&s_capture_stopped,
                                        AUDIO_TASK_STOP_TIMEOUT_MS);
    int play_rc = rtos_get_semaphore(&s_play_stopped,
                                     AUDIO_TASK_STOP_TIMEOUT_MS);
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
            "audio stopped up=%u down=%u dropped=%u aec=%u ref-under=%u ref-over=%u\n",
            (unsigned)s_uplink_frames, (unsigned)s_downlink_frames,
            (unsigned)s_dropped_bytes, (unsigned)s_aec_processed_frames,
            (unsigned)s_aec_reference_underruns,
            (unsigned)s_aec_reference_overruns);
    return BK_OK;
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
    return xiaotai_audio_playback_drained(
        running, queued,
        atomic_load_explicit(&s_playback_in_progress, memory_order_acquire),
        atomic_load_explicit(&s_playback_tail_ms, memory_order_acquire),
        rtos_get_time(), quiet_ms);
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
            "AI playback stage=%s rx=%u written=%u overflow=%u pending=%u peak=%u slots=%u active=%d pcm-ms=%u tail-ms=%u last-rx-age-ms=%u\n",
            stage, (unsigned)atomic_load(&s_opus_received_frames),
            (unsigned)atomic_load(&s_downlink_frames),
            (unsigned)dropped, (unsigned)queued, (unsigned)peak,
            (unsigned)OPUS_PACKET_SLOTS,
            atomic_load(&s_playback_in_progress) ? 1 : 0,
            (unsigned)atomic_load(&s_playback_pcm_ms),
            (unsigned)(tail_ms > 0 ? tail_ms : 0),
            (unsigned)(now_ms - atomic_load(&s_last_opus_rx_ms)));
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
