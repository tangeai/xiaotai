/*
 * 立创·实战派 ESP32-S3 V1.0.1 媒体适配器。
 *
 * 上行：板载 GC0308/实机探测 GC2145 RGB565 -> software JPEG ->
 *       TiRTC JPEG stream 11；
 *       ES7210 TDM MIC1 + MIC3 硬件回采 -> ESP-SR AEC 16 kHz ->
 *       PCM 8 kHz mono -> G.711 A-law。
 * 下行：G.711 A-law -> PCM 8 kHz mono -> 16 kHz -> ES8311 -> NS4150B。
 *
 * ES7210 的四槽 TDM 与 ES8311 的标准 I2S 共用 MCLK/BCLK/WS。两路一次性
 * 注册为 I2S0 配对通道，由同一个控制器输出共享时钟，采集和播放可同时运行。
 * 板上的 ES8311 差分输出经电阻网络接入 ES7210 MIC3，作为同步 AEC 参考。
 */
#include "starter_media.h"
#include "starter_aec.h"
#include "starter_jpeg_collector.h"
#include "starter_voice.h"
#include "starter_preroll.h"
#include "board_config.h"
#include "board_adapter.h"

#include <math.h>
#include <stdatomic.h>
#include <stdlib.h>
#include <string.h>

#include "decoder/impl/esp_g711_dec.h"
#include "driver/gpio.h"
#include "driver/i2c_master.h"
#include "encoder/impl/esp_g711_enc.h"
#include "esp_attr.h"
#include "esp_camera.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_memory_utils.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "img_converters.h"

#define AUDIO_TRANSPORT_SAMPLE_RATE_HZ 8000U
#define AUDIO_PACKET_MS 20U
#define AUDIO_PACKET_SAMPLES \
    ((AUDIO_TRANSPORT_SAMPLE_RATE_HZ * AUDIO_PACKET_MS) / 1000U)
#define AUDIO_RX_BYTES 1500U
#define AUDIO_RX_QUEUE_DEPTH 8U
#define AUDIO_TX_QUEUE_DEPTH 12U
#define AUDIO_PLAYBACK_UPSAMPLE 2U
#define AUDIO_PLAYBACK_I2S_VALUES_PER_INPUT (AUDIO_PLAYBACK_UPSAMPLE * 2U)
#define AUDIO_PLAYBACK_IDLE_MS 60U
#define MEDIA_CPU_YIELD_MS 1U
#define MEDIA_REALTIME_CORE 1

_Static_assert(AUDIO_HW_SAMPLE_RATE_HZ == STARTER_AEC_SAMPLE_RATE_HZ,
               "I2S and AEC sample rates must match");
_Static_assert(AUDIO_TRANSPORT_SAMPLE_RATE_HZ ==
                   STARTER_AEC_TRANSPORT_RATE_HZ,
               "AEC output and TiRTC sample rates must match");
_Static_assert(AUDIO_TDM_SLOTS == 4U,
               "ES7210 physical TDM wire layout must remain four-slot");
_Static_assert(STARTER_AEC_CAPTURE_DMA_CHANNELS == 2U,
               "selected ES7210 slot mask must be compacted to MIC1/MIC3 DMA pairs");

#define CAMERA_JPEG_QUALITY 80U
/* 实时查看只需要顺畅的状态感知；限制为 8 FPS，给 AEC、语音和网络控制
 * 留出确定的 CPU 预算，禁止以提高帧率牺牲通话稳定性。 */
#define CAMERA_TARGET_FPS 8U
#define CAMERA_FRAME_INTERVAL_MS (1000U / CAMERA_TARGET_FPS)
#define VIDEO_BACKPRESSURE_BYTES (192U * 1024U)
#define JPEG_BUFFER_BYTES (128U * 1024U)


typedef struct {
    starter_tirtc_mode_t mode;
    uint32_t generation;
    starter_tirtc_frame_t frame;
    uint8_t payload[AUDIO_RX_BYTES];
} audio_rx_item_t;

/*
 * 采集/AEC 是硬实时任务，不能直接进入 TiRTC SDK。每个项目固定为一个
 * 20 ms、8 kHz、16-bit 单声道 PCM 包；池放在 PSRAM，队列只传递索引。
 */
typedef struct {
    starter_tirtc_mode_t mode;
    uint32_t generation;
    uint32_t timestamp_ms;
    unsigned mute_epoch;
    int16_t pcm[AUDIO_PACKET_SAMPLES];
} audio_tx_item_t;

static const char *TAG = "starter_media";

static atomic_bool s_ready;
static const xiaotai_board_audio_adapter_t *s_audio_adapter;
static xiaotai_board_audio_format_t s_board_audio_format;
static const xiaotai_board_camera_adapter_t *s_camera_adapter;
static atomic_bool s_wake_allowed = true;
static SemaphoreHandle_t s_preroll_mutex;
static starter_preroll_t s_preroll;
static uint32_t s_expected_wake_token; /* protected by preroll mutex */
static atomic_uint s_mute_epoch;
static atomic_bool s_preroll_gap;
static atomic_uint s_capture_epoch;
static atomic_bool s_active;
static atomic_bool s_video_refresh_requested;
static atomic_int s_mode;
static atomic_uint_fast32_t s_generation;
static atomic_uint_fast32_t s_audio_sent;
static atomic_uint_fast32_t s_video_sent;
static atomic_uint_fast32_t s_audio_received;
static atomic_uint_fast32_t s_audio_dropped;
static atomic_uint_fast32_t s_audio_decoded;
static atomic_uint_fast32_t s_audio_played;
static atomic_uint_fast32_t s_audio_decode_failed;
static atomic_uint_fast32_t s_audio_playback_blocked;
static atomic_uint_fast32_t s_audio_write_failed;
static atomic_uint_fast32_t s_aec_processed;
/* [DEBUG-wake49] Capture-owned accumulator, fixed-size published snapshot. */
static starter_media_audio_diagnostics_t s_audio_diag_acc, s_audio_diag;
static portMUX_TYPE s_audio_diag_lock = portMUX_INITIALIZER_UNLOCKED;

static void update_audio_diagnostics(const starter_aec_output_t *frame,
                                     uint32_t aec_us, uint32_t captured_ms)
{
    for (unsigned i = 0; i < 3; ++i) {
        s_audio_diag_acc.rms_avg[i] += frame->level[i].rms;
        s_audio_diag_acc.dc_avg[i] += frame->level[i].dc;
        if (frame->level[i].rms > s_audio_diag_acc.rms_max[i])
            s_audio_diag_acc.rms_max[i] = frame->level[i].rms;
        if (frame->level[i].peak > s_audio_diag_acc.peak[i])
            s_audio_diag_acc.peak[i] = frame->level[i].peak;
    }
    s_audio_diag_acc.aec_avg_us += aec_us;
    s_audio_diag_acc.measurement_avg_us += frame->measurement_us;
    if (frame->measurement_us > s_audio_diag_acc.measurement_max_us)
        s_audio_diag_acc.measurement_max_us = frame->measurement_us;
    if (++s_audio_diag_acc.frames < 32U) return;
    for (unsigned i = 0; i < 3; ++i) {
        s_audio_diag_acc.rms_avg[i] /= 32U;
        s_audio_diag_acc.dc_avg[i] /= 32;
    }
    s_audio_diag_acc.aec_avg_us /= 32U;
    s_audio_diag_acc.measurement_avg_us /= 32U;
    s_audio_diag_acc.at_ms = captured_ms;
    portENTER_CRITICAL(&s_audio_diag_lock);
    s_audio_diag_acc.window = s_audio_diag.window + 1U;
    s_audio_diag = s_audio_diag_acc;
    portEXIT_CRITICAL(&s_audio_diag_lock);
    memset(&s_audio_diag_acc, 0, sizeof(s_audio_diag_acc));
}

void starter_media_audio_diagnostics(starter_media_audio_diagnostics_t *out)
{
    if (out == NULL) return;
    portENTER_CRITICAL(&s_audio_diag_lock);
    *out = s_audio_diag;
    portEXIT_CRITICAL(&s_audio_diag_lock);
}
static atomic_uint_fast32_t s_aec_errors;
static atomic_uint_fast32_t s_aec_mic_clipped;
static atomic_uint_fast32_t s_aec_reference_clipped;
static atomic_uint_fast32_t s_aec_max_process_us;
static atomic_uint_fast32_t s_aec_deadline_misses;
static atomic_uint_fast32_t s_jpeg_max_encode_us;
static atomic_uint_fast32_t s_jpeg_deadline_misses;
static atomic_uchar s_audio_activity_level;
static atomic_bool s_voice_active;
static atomic_uchar s_speaker_volume = 7;
static atomic_bool s_speaker_muted;
static atomic_bool s_microphone_muted;
static atomic_bool s_uplink_enabled;
/* 单调序号避免“清除取消标志”与新铃声启动之间的竞态。 */
static atomic_uint_fast32_t s_pcm8k_cancel_sequence;

static audio_rx_item_t *s_audio_rx_pool;
static audio_tx_item_t *s_audio_tx_pool;
static QueueHandle_t s_audio_rx_ready_queue;
static QueueHandle_t s_audio_rx_free_queue;
static QueueHandle_t s_audio_tx_ready_queue;
static QueueHandle_t s_audio_tx_free_queue;
static SemaphoreHandle_t s_audio_output_mutex;
static bool s_amp_enabled;
static void *s_g711_encoder;
static void *s_g711_decoder;
static uint8_t *s_jpeg_buffer;

/* 下行会话和启动期绑定播报复用这些缓冲，不放到任务栈上。 */
EXT_RAM_BSS_ATTR static int16_t s_decode_pcm[AUDIO_RX_BYTES];
/* ES8311 consumes ordinary interleaved 16-bit stereo PCM.  Keep this exactly
 * aligned with the validated device-monitor board adapter. */
EXT_RAM_BSS_ATTR static int16_t
    s_play_stereo[AUDIO_RX_BYTES * AUDIO_PLAYBACK_I2S_VALUES_PER_INPUT];
static uint32_t s_playback_resampler_generation;
static int16_t s_playback_previous;

static void update_max_counter(atomic_uint_fast32_t *counter, uint32_t value)
{
    uint_fast32_t current = atomic_load_explicit(counter, memory_order_relaxed);
    while (current < value &&
           !atomic_compare_exchange_weak_explicit(counter,
                                                  &current,
                                                  value,
                                                  memory_order_relaxed,
                                                  memory_order_relaxed)) {
    }
}

static bool same_session(starter_tirtc_mode_t mode, uint32_t generation)
{
    return atomic_load_explicit(&s_active, memory_order_acquire) &&
           atomic_load_explicit(&s_mode, memory_order_acquire) == mode &&
           atomic_load_explicit(&s_generation, memory_order_acquire) == generation;
}

static esp_err_t g711_init(void)
{
    esp_g711_enc_config_t encoder = ESP_G711_ENC_CONFIG_DEFAULT();
    encoder.sample_rate = ESP_AUDIO_SAMPLE_RATE_8K;
    encoder.channel = ESP_AUDIO_MONO;
    encoder.bits_per_sample = ESP_AUDIO_BIT16;
    encoder.frame_duration = AUDIO_PACKET_MS;
    if (esp_g711a_enc_open(&encoder, sizeof(encoder), &s_g711_encoder) !=
        ESP_AUDIO_ERR_OK) {
        return ESP_FAIL;
    }
    if (esp_g711_dec_open(NULL, 0, &s_g711_decoder) != ESP_AUDIO_ERR_OK) {
        return ESP_FAIL;
    }
    return ESP_OK;
}

/* 调用者必须持有 s_audio_output_mutex。 */
static esp_err_t audio_output_set_locked(bool enabled)
{
    if (enabled == s_amp_enabled) {
        return ESP_OK;
    }
    esp_err_t err = szpi_board_power_amplifier(enabled);
    if (err != ESP_OK) {
        return err;
    }
    s_amp_enabled = enabled;
    return ESP_OK;
}

static bool enqueue_uplink_pcm(starter_tirtc_mode_t mode,
                               uint32_t generation,
                               uint32_t timestamp_ms,
                               const int16_t *pcm, unsigned mute_epoch)
{
    if (mute_epoch != atomic_load(&s_mute_epoch) || atomic_load(&s_microphone_muted) ||
        !atomic_load(&s_uplink_enabled)) return false;
    uint8_t slot = 0;
    if (pcm == NULL || s_audio_tx_free_queue == NULL ||
        xQueueReceive(s_audio_tx_free_queue, &slot, 0) != pdTRUE) {
        atomic_fetch_add_explicit(&s_audio_dropped, 1, memory_order_relaxed);
        return false;
    }
    audio_tx_item_t *item = &s_audio_tx_pool[slot];
    item->mode = mode;
    item->generation = generation;
    item->timestamp_ms = timestamp_ms;
    item->mute_epoch = mute_epoch;
    memcpy(item->pcm, pcm, sizeof(item->pcm));
    if (xQueueSend(s_audio_tx_ready_queue, &slot, 0) != pdTRUE) {
        memset(item, 0, sizeof(*item));
        (void)xQueueSend(s_audio_tx_free_queue, &slot, 0);
        atomic_fetch_add_explicit(&s_audio_dropped, 1, memory_order_relaxed);
        return false;
    }
    return true;
}

/* Keep TiRTC calls outside the real-time I2S/AEC task, as in device-monitor's
 * rtc_audio_tx worker. The producer only hands off a bounded 20 ms PCM frame. */
static void audio_uplink_task(void *argument)
{
    (void)argument;
    uint8_t alaw[AUDIO_PACKET_SAMPLES];
    for (;;) {
        uint8_t slot = 0;
        if (xQueueReceive(s_audio_tx_ready_queue, &slot, portMAX_DELAY) != pdTRUE) {
            continue;
        }
        audio_tx_item_t *item = &s_audio_tx_pool[slot];
        bool ready = same_session(item->mode, item->generation) &&
                     item->mute_epoch == atomic_load(&s_mute_epoch) &&
                     starter_tirtc_audio_ready() &&
                     !atomic_load_explicit(&s_microphone_muted, memory_order_acquire) &&
                     atomic_load_explicit(&s_uplink_enabled, memory_order_acquire);
        if (ready) {
            esp_audio_enc_in_frame_t input = {
                .buffer = (uint8_t *)item->pcm,
                .len = sizeof(item->pcm),
            };
            esp_audio_enc_out_frame_t output = {
                .buffer = alaw,
                .len = sizeof(alaw),
            };
            esp_err_t encoded = esp_g711_enc_process(s_g711_encoder, &input, &output);
            if (encoded == ESP_AUDIO_ERR_OK && output.encoded_bytes == AUDIO_PACKET_SAMPLES) {
                int ret = starter_tirtc_send_alaw(item->timestamp_ms,
                                                   alaw,
                                                   output.encoded_bytes);
                if (ret >= 0) {
                    uint32_t sent = (uint32_t)atomic_fetch_add_explicit(
                        &s_audio_sent, 1, memory_order_relaxed) + 1U;
                    if (sent == 1U) {
                        ESP_LOGI(TAG,
                                 "uplink first packet mode=%d bytes=%u timestamp=%lu",
                                 (int)item->mode,
                                 (unsigned)output.encoded_bytes,
                                 (unsigned long)item->timestamp_ms);
                    }
                } else {
                    ESP_LOGW(TAG, "uplink send failed mode=%d bytes=%u ret=%d",
                             (int)item->mode, (unsigned)output.encoded_bytes, ret);
                }
            } else {
                ESP_LOGW(TAG, "uplink A-law encode failed ret=%s bytes=%u",
                         esp_err_to_name(encoded), (unsigned)output.encoded_bytes);
            }
        }
        memset(item, 0, sizeof(*item));
        (void)xQueueSend(s_audio_tx_free_queue, &slot, 0);
    }
}

static void audio_capture_task(void *argument)
{
    (void)argument;
    int16_t packet_pcm[AUDIO_PACKET_SAMPLES];
    size_t packet_samples = 0;
    uint32_t packet_generation = 0;
    uint32_t next_timestamp_ms = 0;
    uint32_t smoothed_energy = 0;
    uint8_t candidate_level = 0;
    int64_t candidate_since_ms = 0;
    unsigned mute_epoch = 0;

    for (;;) {
        starter_tirtc_mode_t mode = (starter_tirtc_mode_t)atomic_load_explicit(
            &s_mode, memory_order_acquire);
        uint32_t generation = (uint32_t)atomic_load_explicit(
            &s_generation, memory_order_acquire);
        unsigned current_mute_epoch = atomic_load(&s_mute_epoch);
        unsigned capture_epoch = atomic_load(&s_capture_epoch);
        if (current_mute_epoch != mute_epoch) {
            packet_samples = 0;
            next_timestamp_ms = 0;
            mute_epoch = current_mute_epoch;
        }
        bool session_ready = same_session(mode, generation) &&
                             starter_tirtc_audio_ready();
        if (!session_ready) {
            packet_samples = 0;
            packet_generation = 0;
            next_timestamp_ms = 0;
        }
        if (session_ready && packet_generation != generation) {
            packet_samples = 0;
            packet_generation = generation;
            next_timestamp_ms = 0;
        }

        int16_t *capture = starter_aec_capture_buffer();
        const size_t capture_bytes = starter_aec_capture_bytes();
        size_t captured_bytes = 0U;
        size_t capture_frames = capture_bytes /
            ((size_t)s_board_audio_format.capture_channels * sizeof(int16_t));
        if (s_audio_adapter->read_pcm(s_audio_adapter->context, capture,
                                      capture_frames, &captured_bytes) != ESP_OK ||
            captured_bytes != capture_bytes) {
            atomic_fetch_add_explicit(&s_aec_errors, 1, memory_order_relaxed);
            atomic_store(&s_preroll_gap, true);
            continue;
        }

        const int64_t captured_ms = esp_timer_get_time() / 1000;
        starter_aec_output_t clean = {0};
        int64_t aec_start_us = esp_timer_get_time();
        esp_err_t aec_err = starter_aec_process_capture(capture_bytes, &clean);
        uint32_t aec_elapsed_us = (uint32_t)(esp_timer_get_time() - aec_start_us);
        update_max_counter(&s_aec_max_process_us, aec_elapsed_us);
        uint32_t aec_deadline_us = (uint32_t)(
            starter_aec_frame_samples() * 1000000U / AUDIO_HW_SAMPLE_RATE_HZ);
        if (aec_elapsed_us > aec_deadline_us) {
            atomic_fetch_add_explicit(&s_aec_deadline_misses,
                                      1,
                                      memory_order_relaxed);
        }
        /*
         * AEC 慢于 32 ms 输入节拍时，DMA 会始终有数据，I2S read 不再阻塞。
         * 强制阻塞一个 tick，避免 board_audio_tx 长时间占满所在核心。
         */
        vTaskDelay(pdMS_TO_TICKS(MEDIA_CPU_YIELD_MS));
        if (aec_err != ESP_OK) {
            atomic_fetch_add_explicit(&s_aec_errors, 1, memory_order_relaxed);
            atomic_store(&s_preroll_gap, true);
            continue;
        }
        update_audio_diagnostics(&clean, aec_elapsed_us, (uint32_t)captured_ms);
        if (mute_epoch != atomic_load(&s_mute_epoch) ||
            capture_epoch != atomic_load(&s_capture_epoch)) continue;
        atomic_fetch_add_explicit(&s_aec_processed, 1, memory_order_relaxed);
        atomic_fetch_add_explicit(&s_aec_mic_clipped,
                                  clean.mic_clipped,
                                  memory_order_relaxed);
        atomic_fetch_add_explicit(&s_aec_reference_clipped,
                                  clean.reference_clipped,
                                  memory_order_relaxed);

        /* Bounded PCM copy only under this mutex; never hold it across codec,
         * network or playback calls. A skipped capture invalidates continuity. */
        if (xSemaphoreTake(s_preroll_mutex, 0) == pdTRUE) {
            if (capture_epoch != atomic_load(&s_capture_epoch) ||
                mute_epoch != atomic_load(&s_mute_epoch)) {
                xSemaphoreGive(s_preroll_mutex);
                continue;
            }
            if (s_expected_wake_token == 0 && s_preroll.token != 0 &&
                (s_preroll.failed || captured_ms > s_preroll.expires_ms)) {
                starter_preroll_clear(&s_preroll);
            }
            if (atomic_exchange(&s_preroll_gap, false)) {
                if (s_preroll.token != 0) s_preroll.failed = true;
                else starter_preroll_clear(&s_preroll);
            }
            bool muted = atomic_load(&s_microphone_muted);
            if (muted) {
                starter_preroll_clear(&s_preroll);
            } else if (s_preroll.token != 0 ||
                       (!atomic_load(&s_active) && atomic_load(&s_wake_allowed))) {
                starter_preroll_append(&s_preroll, clean.pcm_8k, clean.samples, captured_ms);
            }
            if (s_preroll.failed && s_preroll.generation != 0) {
                /* Already admitted: lose damaged backlog, never the AI session.
                 * The current clean frame goes through the normal live path. */
                starter_preroll_clear(&s_preroll);
                s_expected_wake_token = 0;
                packet_samples = 0;
                next_timestamp_ms = 0;
                atomic_fetch_add(&s_audio_dropped, 1);
                ESP_LOGW(TAG, "AI replay interrupted after admission; recovering live audio");
            }
            xSemaphoreGive(s_preroll_mutex);
        } else {
            atomic_store(&s_preroll_gap, true);
        }

        /* Only copy to the dedicated wake worker; never run TFLite in capture. */
        bool wake_listen = !atomic_load(&s_active) && atomic_load(&s_wake_allowed) &&
                           !atomic_load(&s_microphone_muted);
        starter_voice_set_listening(wake_listen);
        if (wake_listen && starter_voice_ready())
            (void)starter_voice_feed_pcm16k(clean.pcm_16k, clean.samples_16k, captured_ms);

        /*
         * 产品 UI 只把 RMS 当作“有声音”提示，绝不据此推断情绪。用 IIR 平滑，
         * 并要求档位保持 300 ms 才发布，避免 20~40 ms 抖动让屏幕反复跳变。
         */
        uint64_t square_sum = 0;
        for (size_t i = 0; i < clean.samples; ++i) {
            int32_t sample = clean.pcm_8k[i];
            square_sum += (uint64_t)((int64_t)sample * sample);
        }
        uint32_t energy = clean.samples == 0U ? 0U :
                          (uint32_t)sqrtf((float)square_sum /
                                          (float)clean.samples);
        smoothed_energy = (smoothed_energy * 3U + energy) / 4U;
        uint8_t measured_level = smoothed_energy < 100U ? 0U :
                                 smoothed_energy < 300U ? 1U :
                                 smoothed_energy < 850U ? 2U : 3U;
        int64_t activity_now_ms = esp_timer_get_time() / 1000;
        if (measured_level != candidate_level) {
            candidate_level = measured_level;
            candidate_since_ms = activity_now_ms;
        } else if (activity_now_ms - candidate_since_ms >= 300) {
            atomic_store_explicit(&s_audio_activity_level,
                                  candidate_level,
                                  memory_order_release);
            atomic_store_explicit(&s_voice_active,
                                  candidate_level >= 2U,
                                  memory_order_release);
        }

        mode = (starter_tirtc_mode_t)atomic_load_explicit(&s_mode,
                                                           memory_order_acquire);
        generation = (uint32_t)atomic_load_explicit(&s_generation,
                                                     memory_order_acquire);
        session_ready = same_session(mode, generation) &&
                        starter_tirtc_audio_ready() &&
                        !atomic_load_explicit(&s_microphone_muted,
                                              memory_order_acquire) &&
                        atomic_load_explicit(&s_uplink_enabled,
                                             memory_order_acquire);
        if (!session_ready) {
            continue;
        }

        bool replay = false;
        if (mode == STARTER_TIRTC_AI) {
            /* Catch up, then transfer the partial packet exactly once to live. */
            if (xSemaphoreTake(s_preroll_mutex, 0) != pdTRUE) continue;
            replay = s_expected_wake_token != 0;
            if (replay) {
                packet_samples = 0;
                for (unsigned packet = 0; packet < 3U; ++packet) {
                    uint32_t timestamp;
                    if (!starter_preroll_peek(&s_preroll, generation, packet_pcm,
                                              AUDIO_PACKET_SAMPLES, &timestamp) ||
                        !enqueue_uplink_pcm(mode, generation, timestamp, packet_pcm, mute_epoch)) break;
                    starter_preroll_consume(&s_preroll, AUDIO_PACKET_SAMPLES);
                }
                if (starter_preroll_finish_replay(&s_preroll, generation, packet_pcm,
                                                 AUDIO_PACKET_SAMPLES, &packet_samples,
                                                 &next_timestamp_ms)) {
                    s_expected_wake_token = 0;
                    /* Keep replay=true for this iteration: clean was already
                     * appended above and must not be transmitted a second time. */
                    ESP_LOGI(TAG, "AI preroll complete; live audio generation=%lu",
                             (unsigned long)generation);
                }
            }
            xSemaphoreGive(s_preroll_mutex);
        }
        if (replay) continue;

        for (size_t i = 0; i < clean.samples; ++i) {
            packet_pcm[packet_samples++] = clean.pcm_8k[i];
            if (packet_samples != AUDIO_PACKET_SAMPLES) {
                continue;
            }
            packet_samples = 0;
            if (!same_session(mode, generation) || !starter_tirtc_audio_ready()) {
                continue;
            }
            if (next_timestamp_ms == 0U) {
                next_timestamp_ms = (uint32_t)(esp_timer_get_time() / 1000);
            }
            (void)enqueue_uplink_pcm(mode, generation, next_timestamp_ms, packet_pcm, mute_epoch);
            next_timestamp_ms += AUDIO_PACKET_MS;
        }
    }
}

static bool play_audio_item(const audio_rx_item_t *item)
{
    if (item == NULL || !same_session(item->mode, item->generation)) {
        return false;
    }
    esp_audio_dec_in_raw_t input = {
        .buffer = (uint8_t *)item->payload,
        .len = item->frame.length,
    };
    esp_audio_dec_out_frame_t output = {
        .buffer = (uint8_t *)s_decode_pcm,
        .len = sizeof(s_decode_pcm),
    };
    esp_audio_dec_info_t info = {0};
    if (esp_g711a_dec_decode(s_g711_decoder, &input, &output, &info) !=
            ESP_AUDIO_ERR_OK ||
        output.decoded_size == 0U) {
        atomic_fetch_add_explicit(&s_audio_decode_failed, 1, memory_order_relaxed);
        return false;
    }
    atomic_fetch_add_explicit(&s_audio_decoded, 1, memory_order_relaxed);

    size_t mono_samples = output.decoded_size / sizeof(int16_t);
    if (mono_samples > AUDIO_RX_BYTES) {
        return false;
    }
    if (s_playback_resampler_generation != item->generation) {
        s_playback_resampler_generation = item->generation;
        s_playback_previous = s_decode_pcm[0];
    }
    for (size_t i = 0; i < mono_samples; ++i) {
        int16_t current = s_decode_pcm[i];
        int16_t midpoint = (int16_t)(((int32_t)s_playback_previous + current) / 2);
        size_t output_index = i * AUDIO_PLAYBACK_I2S_VALUES_PER_INPUT;
        s_play_stereo[output_index] = current;
        s_play_stereo[output_index + 1U] = current;
        s_play_stereo[output_index + 2U] = midpoint;
        s_play_stereo[output_index + 3U] = midpoint;
        s_playback_previous = current;
    }

    if (xSemaphoreTake(s_audio_output_mutex, pdMS_TO_TICKS(500)) != pdTRUE) {
        return false;
    }
    bool played = false;
    if (same_session(item->mode, item->generation) && s_amp_enabled &&
        !atomic_load_explicit(&s_speaker_muted, memory_order_acquire)) {
        size_t bytes = mono_samples *
                       AUDIO_PLAYBACK_I2S_VALUES_PER_INPUT * sizeof(int16_t);
        size_t frames = bytes /
            ((size_t)s_board_audio_format.playback_channels * sizeof(int16_t));
        size_t written = 0U;
        played = s_audio_adapter->write_pcm(
            s_audio_adapter->context, s_play_stereo, frames, &written) == ESP_OK &&
            written == bytes;
        if (played) {
            atomic_fetch_add_explicit(&s_audio_played, 1, memory_order_relaxed);
        } else {
            atomic_fetch_add_explicit(&s_audio_write_failed, 1,
                                      memory_order_relaxed);
        }
    } else {
        atomic_fetch_add_explicit(&s_audio_playback_blocked, 1,
                                  memory_order_relaxed);
    }
    xSemaphoreGive(s_audio_output_mutex);
    return played;
}

static void audio_sink_task(void *argument)
{
    (void)argument;
    for (;;) {
        uint8_t slot = 0;
        if (xQueueReceive(s_audio_rx_ready_queue, &slot, portMAX_DELAY) != pdTRUE ||
            slot >= AUDIO_RX_QUEUE_DEPTH) {
            continue;
        }
        do {
            audio_rx_item_t *item = &s_audio_rx_pool[slot];
            (void)play_audio_item(item);
            memset(item, 0, sizeof(*item));
            if (xQueueSend(s_audio_rx_free_queue, &slot, 0) != pdTRUE) {
                ESP_LOGE(TAG, "audio RX slot %u could not be returned",
                         (unsigned)slot);
            }
        } while (xQueueReceive(s_audio_rx_ready_queue,
                               &slot,
                               pdMS_TO_TICKS(AUDIO_PLAYBACK_IDLE_MS)) == pdTRUE);

    }
}

static void camera_task(void *argument)
{
    (void)argument;
    int64_t last_frame_ms = 0;
    for (;;) {
        starter_tirtc_mode_t mode = (starter_tirtc_mode_t)atomic_load_explicit(
            &s_mode, memory_order_acquire);
        uint32_t generation = (uint32_t)atomic_load_explicit(
            &s_generation, memory_order_acquire);
        bool refresh = atomic_exchange_explicit(&s_video_refresh_requested,
                                                 false,
                                                 memory_order_acq_rel);
        int64_t now_ms = esp_timer_get_time() / 1000;
        if (!same_session(mode, generation) || mode != STARTER_TIRTC_H5 ||
            !starter_tirtc_video_ready() ||
            (!refresh && now_ms - last_frame_ms < CAMERA_FRAME_INTERVAL_MS)) {
            vTaskDelay(pdMS_TO_TICKS(20));
            continue;
        }
        if (starter_tirtc_send_buffer_used() > VIDEO_BACKPRESSURE_BYTES) {
            vTaskDelay(pdMS_TO_TICKS(20));
            continue;
        }

        xiaotai_board_camera_frame_t frame = {0};
        if (s_camera_adapter == NULL ||
            s_camera_adapter->acquire(s_camera_adapter->context, &frame) != ESP_OK) {
            ESP_LOGW(TAG, "camera frame acquisition timed out");
            vTaskDelay(pdMS_TO_TICKS(20));
            continue;
        }
        if (!xiaotai_board_camera_frame_is_valid(&frame) ||
            frame.format != XIAOTAI_CAMERA_FRAME_RGB565) {
            s_camera_adapter->release(s_camera_adapter->context, &frame);
            ESP_LOGW(TAG, "camera returned unsupported frame");
            vTaskDelay(pdMS_TO_TICKS(20));
            continue;
        }
        camera_fb_t native_frame = {
            .buf = (uint8_t *)frame.data,
            .len = frame.size,
            .width = frame.width,
            .height = frame.height,
            .format = PIXFORMAT_RGB565,
        };
        starter_jpeg_collector_t jpeg = {
            .buffer = s_jpeg_buffer,
            .capacity = JPEG_BUFFER_BYTES,
        };
        int64_t jpeg_start_us = esp_timer_get_time();
        bool converted = frame2jpg_cb(&native_frame,
                                      CAMERA_JPEG_QUALITY,
                                      starter_jpeg_collect,
                                      &jpeg);
        uint32_t jpeg_elapsed_us = (uint32_t)(esp_timer_get_time() - jpeg_start_us);
        update_max_counter(&s_jpeg_max_encode_us, jpeg_elapsed_us);
        if (jpeg_elapsed_us > CAMERA_FRAME_INTERVAL_MS * 1000U) {
            atomic_fetch_add_explicit(&s_jpeg_deadline_misses,
                                      1,
                                      memory_order_relaxed);
        }
        s_camera_adapter->release(s_camera_adapter->context, &frame);
        /* 软件 JPEG 达不到 8 fps 时也必须给 IDLE/RTC 留出调度窗口。 */
        vTaskDelay(pdMS_TO_TICKS(MEDIA_CPU_YIELD_MS));
        if (!converted || jpeg.length == 0U) {
            ESP_LOGW(TAG, "RGB565 to JPEG conversion failed");
            continue;
        }
        if (jpeg.overflow) {
            ESP_LOGW(TAG,
                     "JPEG output exceeded %u-byte PSRAM buffer",
                     (unsigned)JPEG_BUFFER_BYTES);
            continue;
        }
        if (same_session(mode, generation) && starter_tirtc_video_ready() &&
            starter_tirtc_send_mjpeg((uint32_t)now_ms,
                                      s_jpeg_buffer,
                                      jpeg.length) >= 0) {
            atomic_fetch_add_explicit(&s_video_sent, 1, memory_order_relaxed);
            last_frame_ms = now_ms;
        }
    }
}

static void drain_audio_rx_ready_queue(void)
{
    if (s_audio_rx_ready_queue == NULL || s_audio_rx_free_queue == NULL ||
        s_audio_rx_pool == NULL) {
        return;
    }
    uint8_t slot = 0;
    while (xQueueReceive(s_audio_rx_ready_queue, &slot, 0) == pdTRUE) {
        if (slot < AUDIO_RX_QUEUE_DEPTH) {
            memset(&s_audio_rx_pool[slot], 0, sizeof(s_audio_rx_pool[slot]));
            (void)xQueueSend(s_audio_rx_free_queue, &slot, 0);
        }
    }
}

esp_err_t starter_media_init(void)
{
    if (atomic_load_explicit(&s_ready, memory_order_acquire)) {
        return ESP_OK;
    }
    s_audio_output_mutex = xSemaphoreCreateMutex();
    if (s_audio_output_mutex == NULL) {
        return ESP_ERR_NO_MEM;
    }
    esp_err_t err = szpi_board_init();
    s_audio_adapter = szpi_board_audio_adapter();
    if (err == ESP_OK && s_audio_adapter == NULL) {
        err = ESP_ERR_INVALID_STATE;
    }
    if (err == ESP_OK) {
        err = s_audio_adapter->start(s_audio_adapter->context);
    }
    if (err == ESP_OK) {
        s_board_audio_format =
            s_audio_adapter->format(s_audio_adapter->context);
        if (!s_audio_adapter->ready(s_audio_adapter->context) ||
            !xiaotai_board_audio_format_is_valid(&s_board_audio_format) ||
            s_board_audio_format.sample_rate_hz != STARTER_AEC_SAMPLE_RATE_HZ ||
            s_board_audio_format.capture_channels != STARTER_AEC_CAPTURE_DMA_CHANNELS ||
            s_board_audio_format.playback_channels != 2U ||
            s_board_audio_format.capture_layout !=
                XIAOTAI_CAPTURE_MIC_PLAYBACK_REFERENCE) {
            err = ESP_ERR_INVALID_STATE;
        }
    }
    if (err == ESP_OK) {
        err = g711_init();
    }
    if (err == ESP_OK) {
        s_camera_adapter = szpi_board_camera_adapter();
        err = s_camera_adapter == NULL ? ESP_ERR_INVALID_STATE :
            s_camera_adapter->start(s_camera_adapter->context);
    }
    if (err == ESP_OK &&
        !s_camera_adapter->ready(s_camera_adapter->context)) {
        err = ESP_ERR_INVALID_STATE;
    }
    if (err == ESP_OK) {
        err = starter_aec_init();
    }
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "board media initialization failed: %s", esp_err_to_name(err));
        return err;
    }

    s_audio_rx_pool = heap_caps_calloc(AUDIO_RX_QUEUE_DEPTH,
                                       sizeof(*s_audio_rx_pool),
                                       MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    s_audio_tx_pool = heap_caps_calloc(AUDIO_TX_QUEUE_DEPTH,
                                       sizeof(*s_audio_tx_pool),
                                       MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    int16_t *preroll_pcm = heap_caps_calloc(PREROLL_CAPACITY_SAMPLES,
                                           sizeof(int16_t),
                                           MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    s_preroll_mutex = xSemaphoreCreateMutex();
    if (preroll_pcm == NULL || s_preroll_mutex == NULL) return ESP_ERR_NO_MEM;
    starter_preroll_init(&s_preroll, preroll_pcm, PREROLL_CAPACITY_SAMPLES);
    s_jpeg_buffer = heap_caps_malloc(JPEG_BUFFER_BYTES,
                                     MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    s_audio_rx_ready_queue = xQueueCreate(AUDIO_RX_QUEUE_DEPTH, sizeof(uint8_t));
    s_audio_rx_free_queue = xQueueCreate(AUDIO_RX_QUEUE_DEPTH, sizeof(uint8_t));
    s_audio_tx_ready_queue = xQueueCreate(AUDIO_TX_QUEUE_DEPTH, sizeof(uint8_t));
    s_audio_tx_free_queue = xQueueCreate(AUDIO_TX_QUEUE_DEPTH, sizeof(uint8_t));
    if (s_audio_rx_pool == NULL || s_audio_tx_pool == NULL || s_jpeg_buffer == NULL ||
        !esp_ptr_external_ram(s_audio_rx_pool) ||
        !esp_ptr_external_ram(s_audio_tx_pool) ||
        !esp_ptr_external_ram(s_jpeg_buffer) ||
        !esp_ptr_external_ram(s_decode_pcm) ||
        !esp_ptr_external_ram(s_play_stereo) ||
        s_audio_rx_ready_queue == NULL || s_audio_rx_free_queue == NULL ||
        s_audio_tx_ready_queue == NULL || s_audio_tx_free_queue == NULL) {
        return ESP_ERR_NO_MEM;
    }
    for (uint8_t slot = 0; slot < AUDIO_RX_QUEUE_DEPTH; ++slot) {
        if (xQueueSend(s_audio_rx_free_queue, &slot, 0) != pdTRUE) {
            return ESP_FAIL;
        }
    }
    for (uint8_t slot = 0; slot < AUDIO_TX_QUEUE_DEPTH; ++slot) {
        if (xQueueSend(s_audio_tx_free_queue, &slot, 0) != pdTRUE) {
            return ESP_FAIL;
        }
    }
    /* These long-running workers use only PSRAM-backed frame pools and board
     * drivers. Keeping their stacks in PSRAM preserves internal/DMA SRAM for
     * Wi-Fi and TiRTC bootstrap allocations. */
    if (xTaskCreateWithCaps(audio_sink_task,
                            "board_audio_rx",
                            6144,
                            NULL,
                            8,
                            NULL,
                            MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT) != pdPASS ||
        xTaskCreateWithCaps(audio_uplink_task,
                            "rtc_audio_tx",
                            6144,
                            NULL,
                            7,
                            NULL,
                            MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT) != pdPASS ||
        xTaskCreatePinnedToCoreWithCaps(audio_capture_task,
                                        "board_audio_tx",
                                        6144,
                                        NULL,
                                        7,
                                        NULL,
                                        MEDIA_REALTIME_CORE,
                                        MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT) != pdPASS ||
        xTaskCreatePinnedToCoreWithCaps(camera_task,
                                        "board_mjpeg",
                                        8192,
                                        NULL,
                                        5,
                                        NULL,
                                        MEDIA_REALTIME_CORE,
                                        MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT) != pdPASS) {
        return ESP_ERR_NO_MEM;
    }
    atomic_store_explicit(&s_ready, true, memory_order_release);
    ESP_LOGI(TAG,
             "V1.0.1 media ready: GC0308/GC2145 MJPEG target=%u fps, "
             "PSRAM audio pools rx/tx=%u/%u bytes, JPEG=%u bytes, ES7210/ES8311 G.711 A-law, "
             "full-duplex AEC=%u Hz->%u Hz chunk=%u media-core=%d yield=%u ms",
             CAMERA_TARGET_FPS,
             (unsigned)(AUDIO_RX_QUEUE_DEPTH * sizeof(*s_audio_rx_pool)),
             (unsigned)(AUDIO_TX_QUEUE_DEPTH * sizeof(*s_audio_tx_pool)),
             (unsigned)JPEG_BUFFER_BYTES,
             AUDIO_HW_SAMPLE_RATE_HZ,
             AUDIO_TRANSPORT_SAMPLE_RATE_HZ,
             (unsigned)starter_aec_frame_samples(),
             MEDIA_REALTIME_CORE,
             MEDIA_CPU_YIELD_MS);
    return ESP_OK;
}

i2c_master_bus_handle_t starter_media_i2c_bus(void)
{
    return atomic_load_explicit(&s_ready, memory_order_acquire) ?
               szpi_board_i2c_bus() : NULL;
}

esp_err_t starter_media_select_lcd(bool selected)
{
    if (!atomic_load_explicit(&s_ready, memory_order_acquire)) {
        return ESP_ERR_INVALID_STATE;
    }
    if (xSemaphoreTake(s_audio_output_mutex, pdMS_TO_TICKS(500)) != pdTRUE) {
        return ESP_ERR_TIMEOUT;
    }
    esp_err_t err = szpi_board_select_lcd(selected);
    xSemaphoreGive(s_audio_output_mutex);
    return err;
}

esp_err_t starter_media_set_speaker_volume(uint8_t volume)
{
    if (volume > 10U || s_audio_adapter == NULL ||
        !s_audio_adapter->ready(s_audio_adapter->context) ||
        s_audio_adapter->set_volume == NULL || s_audio_output_mutex == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    if (xSemaphoreTake(s_audio_output_mutex, pdMS_TO_TICKS(500)) != pdTRUE) {
        return ESP_ERR_TIMEOUT;
    }
    esp_err_t err = s_audio_adapter->set_volume(s_audio_adapter->context,
                                                volume * 10U);
    if (err == ESP_OK) {
        atomic_store_explicit(&s_speaker_volume, volume, memory_order_release);
    }
    xSemaphoreGive(s_audio_output_mutex);
    return err;
}

esp_err_t starter_media_set_speaker_muted(bool muted)
{
    if (s_audio_adapter == NULL ||
        !s_audio_adapter->ready(s_audio_adapter->context) ||
        s_audio_adapter->set_muted == NULL || s_audio_output_mutex == NULL) {
        return ESP_ERR_INVALID_STATE;
    }
    if (xSemaphoreTake(s_audio_output_mutex, pdMS_TO_TICKS(500)) != pdTRUE) {
        return ESP_ERR_TIMEOUT;
    }
    esp_err_t err = s_audio_adapter->set_muted(s_audio_adapter->context, muted);
    if (err == ESP_OK) {
        atomic_store_explicit(&s_speaker_muted, muted, memory_order_release);
        err = audio_output_set_locked(!muted &&
            atomic_load_explicit(&s_active, memory_order_acquire));
    }
    xSemaphoreGive(s_audio_output_mutex);
    return err;
}

void starter_media_set_microphone_muted(bool muted)
{
    bool previous = atomic_exchange_explicit(&s_microphone_muted, muted, memory_order_acq_rel);
    if (previous != muted) atomic_fetch_add(&s_mute_epoch, 1);
    if (muted) {
        starter_voice_set_listening(false);
        if (s_preroll_mutex != NULL && xSemaphoreTake(s_preroll_mutex, portMAX_DELAY) == pdTRUE) {
            bool was_bound = s_preroll.generation != 0;
            starter_preroll_clear(&s_preroll);
            if (was_bound) s_expected_wake_token = 0;
            /* Keep expected token so the pending AI start fails instead of
             * silently pretending it received the continuous utterance. */
            xSemaphoreGive(s_preroll_mutex);
        }
    }
}

void starter_media_set_wake_allowed(bool allowed)
{
    atomic_store(&s_wake_allowed, allowed);
    if (!allowed) starter_voice_set_listening(false);
}

uint32_t starter_media_prepare_ai_preroll(int64_t captured_ms)
{
    if (s_preroll_mutex == NULL || xSemaphoreTake(s_preroll_mutex, pdMS_TO_TICKS(20)) != pdTRUE) return 0;
    uint32_t token = 0;
    if (!atomic_load(&s_microphone_muted) && !atomic_load(&s_active) &&
        atomic_load(&s_wake_allowed)) {
        token = starter_preroll_prepare(&s_preroll, captured_ms, esp_timer_get_time() / 1000);
    }
    xSemaphoreGive(s_preroll_mutex);
    return token;
}

void starter_media_cancel_ai_preroll(uint32_t token)
{
    if (token == 0 || s_preroll_mutex == NULL) return;
    xSemaphoreTake(s_preroll_mutex, portMAX_DELAY);
    if (s_preroll.token == token && s_preroll.generation == 0 &&
        s_expected_wake_token != token) starter_preroll_clear(&s_preroll);
    xSemaphoreGive(s_preroll_mutex);
}

bool starter_media_ai_preroll_failed(void)
{
    if (s_preroll_mutex == NULL || xSemaphoreTake(s_preroll_mutex, 0) != pdTRUE) return false;
    bool failed = starter_preroll_failure_requires_abort(&s_preroll,
        s_expected_wake_token, esp_timer_get_time() / 1000);
    xSemaphoreGive(s_preroll_mutex);
    return failed;
}

bool starter_media_preroll_status(starter_media_preroll_status_t *out)
{
    if (out == NULL || s_preroll_mutex == NULL ||
        xSemaphoreTake(s_preroll_mutex, 0) != pdTRUE) return false;
    *out = (starter_media_preroll_status_t){
        .buffered_ms = (uint32_t)(s_preroll.count / 8U),
        .owned = s_preroll.token != 0,
        .bound = s_preroll.generation != 0,
        .failed = s_preroll.failed,
    };
    xSemaphoreGive(s_preroll_mutex);
    return true;
}

void starter_media_cancel_pcm8k_playback(void)
{
    atomic_fetch_add_explicit(&s_pcm8k_cancel_sequence, 1, memory_order_acq_rel);
}

esp_err_t starter_media_play_pcm8k(const int16_t *pcm, size_t sample_count)
{
    return starter_media_play_pcm8k_at_epoch(pcm, sample_count, starter_media_playback_epoch());
}

uint32_t starter_media_playback_epoch(void)
{
    return (uint32_t)atomic_load_explicit(&s_pcm8k_cancel_sequence, memory_order_acquire);
}

esp_err_t starter_media_play_pcm8k_at_epoch(const int16_t *pcm, size_t sample_count,
                                           uint32_t cancel_sequence)
{
    if (pcm == NULL || sample_count == 0U) {
        return ESP_ERR_INVALID_ARG;
    }
    if (!atomic_load_explicit(&s_ready, memory_order_acquire) ||
        s_audio_adapter == NULL ||
        !s_audio_adapter->ready(s_audio_adapter->context) ||
        s_audio_output_mutex == NULL) {
        return ESP_ERR_INVALID_STATE;
    }
    if (xSemaphoreTake(s_audio_output_mutex, pdMS_TO_TICKS(500)) != pdTRUE) {
        return ESP_ERR_TIMEOUT;
    }
    if (atomic_load_explicit(&s_active, memory_order_acquire) ||
        atomic_load_explicit(&s_speaker_muted, memory_order_acquire) ||
        cancel_sequence != atomic_load_explicit(&s_pcm8k_cancel_sequence,
                                                 memory_order_acquire)) {
        xSemaphoreGive(s_audio_output_mutex);
        return ESP_ERR_INVALID_STATE;
    }

    esp_err_t err = audio_output_set_locked(true);
    int16_t previous = pcm[0];
    size_t offset = 0;
    while (err == ESP_OK && offset < sample_count) {
        size_t chunk_samples = sample_count - offset;
        if (chunk_samples > AUDIO_RX_BYTES) {
            chunk_samples = AUDIO_RX_BYTES;
        }
        for (size_t i = 0; i < chunk_samples; ++i) {
            int16_t current = pcm[offset + i];
            int16_t midpoint = (int16_t)(((int32_t)previous + current) / 2);
            size_t output_index = i * AUDIO_PLAYBACK_I2S_VALUES_PER_INPUT;
            s_play_stereo[output_index] = current;
            s_play_stereo[output_index + 1U] = current;
            s_play_stereo[output_index + 2U] = midpoint;
            s_play_stereo[output_index + 3U] = midpoint;
            previous = current;
        }
        size_t bytes = chunk_samples * AUDIO_PLAYBACK_I2S_VALUES_PER_INPUT *
                       sizeof(int16_t);
        size_t frames = bytes /
            ((size_t)s_board_audio_format.playback_channels * sizeof(int16_t));
        size_t written = 0U;
        if (s_audio_adapter->write_pcm(s_audio_adapter->context, s_play_stereo,
                                       frames, &written) != ESP_OK ||
            written != bytes) {
            err = ESP_FAIL;
        }
        offset += chunk_samples;
        if ((uint32_t)atomic_load_explicit(&s_pcm8k_cancel_sequence,
                                           memory_order_acquire) !=
            cancel_sequence) {
            /* 至多等待一个 PCM 块，实时通话随后可获得 I2S 输出锁。 */
            err = ESP_ERR_INVALID_STATE;
            ESP_LOGI(TAG, "PCM8k prompt interrupted for realtime media");
            break;
        }
    }
    if (err == ESP_OK) {
        /* 等待最后一小段 DMA 数据移出，避免关闭功放时切掉尾音。 */
        vTaskDelay(pdMS_TO_TICKS(40));
    }
    esp_err_t disable_err = audio_output_set_locked(false);
    xSemaphoreGive(s_audio_output_mutex);
    if (err == ESP_OK) {
        err = disable_err;
    }
    if (err == ESP_OK) {
        ESP_LOGI(TAG,
                 "verification prompt played samples=%u rate=%u",
                 (unsigned)sample_count,
                 AUDIO_TRANSPORT_SAMPLE_RATE_HZ);
    }
    return err;
}

esp_err_t starter_media_start(starter_tirtc_mode_t mode, uint32_t generation)
{
    if (!atomic_load_explicit(&s_ready, memory_order_acquire) ||
        generation == 0U ||
        (mode != STARTER_TIRTC_H5 && mode != STARTER_TIRTC_AI &&
         mode != STARTER_TIRTC_VOIP && mode != STARTER_TIRTC_CALL &&
         mode != STARTER_TIRTC_ROOM)) {
        return ESP_ERR_INVALID_STATE;
    }
    xSemaphoreTake(s_preroll_mutex, portMAX_DELAY);
    bool preroll_ok = true;
    if (mode == STARTER_TIRTC_AI && s_expected_wake_token != 0) {
        preroll_ok = !atomic_load(&s_microphone_muted) &&
            starter_preroll_bind(&s_preroll, s_expected_wake_token, generation,
                                 esp_timer_get_time() / 1000);
    } else {
        starter_preroll_clear(&s_preroll);
        s_expected_wake_token = 0;
    }
    xSemaphoreGive(s_preroll_mutex);
    if (!preroll_ok) return ESP_ERR_INVALID_STATE;
    if (mode == STARTER_TIRTC_AI) {
        ESP_LOGI(TAG, "AI audio admitted generation=%lu", (unsigned long)generation);
    }
    /* 铃声/验证码提示音是可中断的；实时通话不能因其占用 I2S 而启动失败。 */
    starter_media_cancel_pcm8k_playback();
    drain_audio_rx_ready_queue();
    atomic_store_explicit(&s_audio_sent, 0, memory_order_release);
    atomic_store_explicit(&s_video_sent, 0, memory_order_release);
    atomic_store_explicit(&s_audio_received, 0, memory_order_release);
    atomic_store_explicit(&s_audio_dropped, 0, memory_order_release);
    atomic_store_explicit(&s_audio_decoded, 0, memory_order_release);
    atomic_store_explicit(&s_audio_played, 0, memory_order_release);
    atomic_store_explicit(&s_audio_decode_failed, 0, memory_order_release);
    atomic_store_explicit(&s_audio_playback_blocked, 0, memory_order_release);
    atomic_store_explicit(&s_audio_write_failed, 0, memory_order_release);
    atomic_store_explicit(&s_aec_processed, 0, memory_order_release);
    atomic_store_explicit(&s_aec_errors, 0, memory_order_release);
    atomic_store_explicit(&s_aec_mic_clipped, 0, memory_order_release);
    atomic_store_explicit(&s_aec_reference_clipped, 0, memory_order_release);
    atomic_store_explicit(&s_aec_max_process_us, 0, memory_order_release);
    atomic_store_explicit(&s_aec_deadline_misses, 0, memory_order_release);
    atomic_store_explicit(&s_jpeg_max_encode_us, 0, memory_order_release);
    atomic_store_explicit(&s_jpeg_deadline_misses, 0, memory_order_release);
    atomic_store_explicit(&s_mode, mode, memory_order_release);
    atomic_store_explicit(&s_uplink_enabled, mode != STARTER_TIRTC_ROOM,
                          memory_order_release);
    atomic_store_explicit(&s_generation, generation, memory_order_release);
    atomic_store_explicit(&s_video_refresh_requested, mode == STARTER_TIRTC_H5,
                          memory_order_release);
    atomic_store_explicit(&s_active, true, memory_order_release);
    if (xSemaphoreTake(s_audio_output_mutex, pdMS_TO_TICKS(500)) != pdTRUE) {
        ESP_LOGE(TAG, "media start output lock timeout mode=%d generation=%lu",
                 (int)mode, (unsigned long)generation);
        atomic_store_explicit(&s_active, false, memory_order_release);
        atomic_store_explicit(&s_generation, 0, memory_order_release);
        return ESP_ERR_TIMEOUT;
    }
    esp_err_t err = audio_output_set_locked(
        !atomic_load_explicit(&s_speaker_muted, memory_order_acquire));
    xSemaphoreGive(s_audio_output_mutex);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "media start output enable failed mode=%d generation=%lu err=%s",
                 (int)mode, (unsigned long)generation, esp_err_to_name(err));
        atomic_store_explicit(&s_active, false, memory_order_release);
        atomic_store_explicit(&s_generation, 0, memory_order_release);
        drain_audio_rx_ready_queue();
        return err;
    }
    ESP_LOGI(TAG,
             "media session started mode=%d generation=%lu video=%s",
             (int)mode,
             (unsigned long)generation,
             mode == STARTER_TIRTC_H5 ? "mjpeg" : "disabled");
    return ESP_OK;
}

static esp_err_t stop_media(uint32_t preserve_token)
{
    if (preserve_token == 0) atomic_fetch_add(&s_capture_epoch, 1);
    starter_media_cancel_pcm8k_playback();
    atomic_store_explicit(&s_active, false, memory_order_release);
    atomic_store_explicit(&s_uplink_enabled, false, memory_order_release);
    atomic_store_explicit(&s_generation, 0, memory_order_release);
    bool preserved = preserve_token == 0;
    if (s_preroll_mutex != NULL) {
        xSemaphoreTake(s_preroll_mutex, portMAX_DELAY);
        preserved = preserve_token == 0 ||
                    (!atomic_load(&s_microphone_muted) &&
                     starter_preroll_valid(&s_preroll, preserve_token, esp_timer_get_time() / 1000));
        if (preserve_token == 0 || !preserved) starter_preroll_clear(&s_preroll);
        s_expected_wake_token = preserved ? preserve_token : 0;
        xSemaphoreGive(s_preroll_mutex);
    }
    drain_audio_rx_ready_queue();
    if (s_audio_output_mutex != NULL &&
        xSemaphoreTake(s_audio_output_mutex, pdMS_TO_TICKS(600)) == pdTRUE) {
        (void)audio_output_set_locked(false);
        xSemaphoreGive(s_audio_output_mutex);
    }
    return preserved ? ESP_OK : ESP_ERR_INVALID_STATE;
}

void starter_media_stop(void) { (void)stop_media(0); }
void starter_media_set_uplink_enabled(bool enabled)
{
    atomic_store_explicit(&s_uplink_enabled, enabled, memory_order_release);
}
esp_err_t starter_media_stop_for_ai(uint32_t token) { return stop_media(token); }

void starter_media_request_key_frame(uint32_t generation)
{
    if (generation != 0U &&
        generation == atomic_load_explicit(&s_generation, memory_order_acquire)) {
        atomic_store_explicit(&s_video_refresh_requested, true,
                              memory_order_release);
    }
}

void starter_media_submit_audio(starter_tirtc_mode_t mode,
                                uint32_t generation,
                                const starter_tirtc_frame_t *frame,
                                const void *data)
{
    if (s_audio_rx_ready_queue == NULL || s_audio_rx_free_queue == NULL ||
        s_audio_rx_pool == NULL || frame == NULL || data == NULL ||
        frame->length == 0U || frame->length > AUDIO_RX_BYTES ||
        !same_session(mode, generation)) {
        atomic_fetch_add_explicit(&s_audio_dropped, 1, memory_order_relaxed);
        return;
    }
    uint8_t slot = 0;
    if (xQueueReceive(s_audio_rx_free_queue, &slot, 0) != pdTRUE ||
        slot >= AUDIO_RX_QUEUE_DEPTH) {
        atomic_fetch_add_explicit(&s_audio_dropped, 1, memory_order_relaxed);
        return;
    }
    audio_rx_item_t *item = &s_audio_rx_pool[slot];
    *item = (audio_rx_item_t) {
        .mode = mode,
        .generation = generation,
        .frame = *frame,
    };
    memcpy(item->payload, data, frame->length);
    if (xQueueSend(s_audio_rx_ready_queue, &slot, 0) == pdTRUE) {
        atomic_fetch_add_explicit(&s_audio_received, 1, memory_order_relaxed);
    } else {
        memset(item, 0, sizeof(*item));
        (void)xQueueSend(s_audio_rx_free_queue, &slot, 0);
        atomic_fetch_add_explicit(&s_audio_dropped, 1, memory_order_relaxed);
    }
}

starter_media_status_t starter_media_status(void)
{
    return (starter_media_status_t) {
        .active = atomic_load_explicit(&s_active, memory_order_acquire),
        .mode = (starter_tirtc_mode_t)atomic_load_explicit(&s_mode,
                                                            memory_order_acquire),
        .generation = (uint32_t)atomic_load_explicit(&s_generation,
                                                      memory_order_acquire),
        .audio_sent = (uint32_t)atomic_load_explicit(&s_audio_sent,
                                                      memory_order_acquire),
        .video_sent = (uint32_t)atomic_load_explicit(&s_video_sent,
                                                      memory_order_acquire),
        .audio_received = (uint32_t)atomic_load_explicit(&s_audio_received,
                                                          memory_order_acquire),
        .audio_dropped = (uint32_t)atomic_load_explicit(&s_audio_dropped,
                                                         memory_order_acquire),
        .audio_decoded = (uint32_t)atomic_load_explicit(&s_audio_decoded,
                                                         memory_order_acquire),
        .audio_played = (uint32_t)atomic_load_explicit(&s_audio_played,
                                                        memory_order_acquire),
        .audio_decode_failed = (uint32_t)atomic_load_explicit(
            &s_audio_decode_failed, memory_order_acquire),
        .audio_playback_blocked = (uint32_t)atomic_load_explicit(
            &s_audio_playback_blocked, memory_order_acquire),
        .audio_write_failed = (uint32_t)atomic_load_explicit(
            &s_audio_write_failed, memory_order_acquire),
        .aec_processed = (uint32_t)atomic_load_explicit(&s_aec_processed,
                                                         memory_order_acquire),
        .aec_errors = (uint32_t)atomic_load_explicit(&s_aec_errors,
                                                      memory_order_acquire),
        .aec_mic_clipped = (uint32_t)atomic_load_explicit(&s_aec_mic_clipped,
                                                           memory_order_acquire),
        .aec_reference_clipped = (uint32_t)atomic_load_explicit(
            &s_aec_reference_clipped, memory_order_acquire),
        .aec_max_process_us = (uint32_t)atomic_load_explicit(
            &s_aec_max_process_us, memory_order_acquire),
        .aec_deadline_misses = (uint32_t)atomic_load_explicit(
            &s_aec_deadline_misses, memory_order_acquire),
        .jpeg_max_encode_us = (uint32_t)atomic_load_explicit(
            &s_jpeg_max_encode_us, memory_order_acquire),
        .jpeg_deadline_misses = (uint32_t)atomic_load_explicit(
            &s_jpeg_deadline_misses, memory_order_acquire),
        .audio_activity_level = atomic_load_explicit(
            &s_audio_activity_level, memory_order_acquire),
        .voice_active = atomic_load_explicit(&s_voice_active,
                                              memory_order_acquire),
        .speaker_volume = atomic_load_explicit(&s_speaker_volume,
                                                memory_order_acquire),
        .speaker_muted = atomic_load_explicit(&s_speaker_muted,
                                               memory_order_acquire),
        .microphone_muted = atomic_load_explicit(&s_microphone_muted,
                                                  memory_order_acquire),
        .uplink_enabled = atomic_load_explicit(&s_uplink_enabled,
                                               memory_order_acquire),
    };
}
