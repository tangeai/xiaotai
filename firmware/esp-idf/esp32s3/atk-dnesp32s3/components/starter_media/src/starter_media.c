/* ATK-DNESP32S3 capture/playback adapter. SDK callbacks only enqueue audio. */
#include "starter_media.h"
#include "atk_board.h"
#include "atk_key_gesture.h"
#include "binding_digits.h"

#include <stdatomic.h>
#include <string.h>

#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

#define AUDIO_RX_BYTES 1500U
#define AUDIO_RX_QUEUE_DEPTH 8U
#define AUDIO_SAMPLES_8K_20MS 160U
#define AUDIO_FRAMES_16K_20MS 320U
#define VIDEO_MAX_BYTES (128U * 1024U)
#define VIDEO_SEND_BUFFER_LIMIT (192U * 1024U)
#define BINDING_CODE_MAX 16U

extern const uint8_t s_binding_pcm_start[] asm("_binary_binding_digits_pcm_start");
extern const uint8_t s_binding_pcm_end[] asm("_binary_binding_digits_pcm_end");

/* 队列项拥有 payload 副本，因此 SDK 回调返回后仍可安全消费。 */
typedef struct {
    starter_tirtc_mode_t mode;
    uint32_t generation;
    starter_tirtc_frame_t frame;
    uint8_t payload[AUDIO_RX_BYTES];
} audio_rx_item_t;

static const char *TAG = "starter_media";

/*
 * SDK 回调、会话任务和产品媒体任务会并发访问这些状态。原子变量用于读取
 * 瞬时快照；start/stop 的业务顺序仍由 starter_runtime 单任务保证。
 */
static atomic_bool s_ready;
static atomic_bool s_active;
static atomic_int s_mode;
static atomic_uint_fast32_t s_generation;
static atomic_uint_fast32_t s_audio_sent;
static atomic_uint_fast32_t s_video_sent;
static atomic_uint_fast32_t s_audio_received;
static atomic_uint_fast32_t s_audio_dropped;
static QueueHandle_t s_audio_rx_queue;
static QueueHandle_t s_binding_queue;
static SemaphoreHandle_t s_media_lock;
static const xiaotai_board_audio_adapter_t *s_audio_adapter;
static bool s_board_audio;
static bool s_board_camera;
static const xiaotai_board_camera_adapter_t *s_camera_adapter;
static atomic_bool s_binding_valid;
static char s_binding_code[BINDING_CODE_MAX + 1U];
static starter_media_control_handler_t s_control_handler;
static void *s_control_user_data;

static uint8_t pcm_to_alaw(int16_t pcm)
{
    int32_t value = pcm;
    uint8_t mask = 0xD5;
    if (value < 0) {
        value = -value - 1;
        mask = 0x55;
    }
    if (value > 0x7fff) value = 0x7fff;
    int segment = 0;
    for (int limit = 0xff; segment < 7 && value > limit; ++segment) {
        limit = (limit << 1) + 1;
    }
    uint8_t sample = (uint8_t)(segment << 4);
    sample |= (uint8_t)((value >> (segment ? segment + 3 : 4)) & 0x0f);
    return sample ^ mask;
}

static int16_t alaw_to_pcm(uint8_t sample)
{
    sample ^= 0x55;
    int32_t value = ((sample & 0x0f) << 4) + 8;
    int segment = (sample & 0x70) >> 4;
    if (segment != 0) value = (value + 0x100) << (segment - 1);
    return (int16_t)((sample & 0x80) ? value : -value);
}

static uint32_t timestamp_ms(void)
{
    return (uint32_t)(esp_timer_get_time() / 1000);
}

/* 同时匹配 mode 和 generation，避免上一条连接的迟到音频进入新会话。 */
static bool same_session(starter_tirtc_mode_t mode, uint32_t generation)
{
    return atomic_load_explicit(&s_active, memory_order_acquire) &&
           atomic_load_explicit(&s_mode, memory_order_acquire) == mode &&
           atomic_load_explicit(&s_generation, memory_order_acquire) == generation;
}

static void audio_sink_task(void *argument)
{
    (void)argument;
    audio_rx_item_t item;
    for (;;) {
        if (xQueueReceive(s_audio_rx_queue, &item, portMAX_DELAY) != pdTRUE) {
            continue;
        }

        if (!s_board_audio || item.frame.length > AUDIO_RX_BYTES) continue;
        /* Upsample 8 kHz mono into the codec's 16 kHz stereo I2S slots. */
        if (xSemaphoreTake(s_media_lock, pdMS_TO_TICKS(100)) == pdTRUE) {
            if (same_session(item.mode, item.generation)) {
                int16_t pcm[AUDIO_SAMPLES_8K_20MS * 4U];
                for (size_t offset = 0; offset < item.frame.length &&
                                        same_session(item.mode, item.generation);
                     offset += AUDIO_SAMPLES_8K_20MS) {
                    size_t count = item.frame.length - offset;
                    if (count > AUDIO_SAMPLES_8K_20MS) count = AUDIO_SAMPLES_8K_20MS;
                    for (size_t i = 0; i < count; ++i) {
                        int16_t value = alaw_to_pcm(item.payload[offset + i]);
                        for (size_t j = 0; j < 4; ++j) pcm[i * 4U + j] = value;
                    }
                    size_t written = 0;
                    (void)s_audio_adapter->write_pcm(
                        s_audio_adapter->context, pcm, count * 2U, &written);
                }
            }
            xSemaphoreGive(s_media_lock);
        }
    }
}

static void audio_capture_task(void *argument)
{
    (void)argument;
    int16_t pcm[AUDIO_FRAMES_16K_20MS * 2U];
    uint8_t encoded[AUDIO_SAMPLES_8K_20MS];
    for (;;) {
        if (!atomic_load_explicit(&s_active, memory_order_acquire)) {
            vTaskDelay(pdMS_TO_TICKS(20));
            continue;
        }
        uint32_t generation = (uint32_t)atomic_load_explicit(&s_generation,
                                                               memory_order_acquire);
        size_t bytes = 0;
        if (s_audio_adapter->read_pcm(s_audio_adapter->context, pcm,
                                      AUDIO_FRAMES_16K_20MS, &bytes) != ESP_OK ||
            bytes != sizeof(pcm)) continue;
        for (size_t i = 0; i < AUDIO_SAMPLES_8K_20MS; ++i) {
            /* Vendor recorder routes the same microphone to both ADC slots. */
            int32_t a = pcm[4U * i];
            int32_t b = pcm[4U * i + 2U];
            encoded[i] = pcm_to_alaw((int16_t)((a + b) / 2));
        }
        if (xSemaphoreTake(s_media_lock, pdMS_TO_TICKS(100)) == pdTRUE) {
            if (generation != 0U &&
                generation == atomic_load_explicit(&s_generation, memory_order_acquire) &&
                atomic_load_explicit(&s_active, memory_order_acquire) &&
                starter_tirtc_audio_ready() &&
                starter_tirtc_send_buffer_used() < VIDEO_SEND_BUFFER_LIMIT &&
                starter_tirtc_send_alaw(timestamp_ms(), encoded, sizeof(encoded)) == 0) {
                atomic_fetch_add_explicit(&s_audio_sent, 1, memory_order_relaxed);
            }
            xSemaphoreGive(s_media_lock);
        }
    }
}

static void video_capture_task(void *argument)
{
    (void)argument;
    for (;;) {
        if (!atomic_load_explicit(&s_active, memory_order_acquire) ||
            atomic_load_explicit(&s_mode, memory_order_acquire) != STARTER_TIRTC_H5 ||
            !starter_tirtc_video_ready() ||
            starter_tirtc_send_buffer_used() >= VIDEO_SEND_BUFFER_LIMIT) {
            vTaskDelay(pdMS_TO_TICKS(100));
            continue;
        }
        uint32_t generation = (uint32_t)atomic_load_explicit(&s_generation,
                                                               memory_order_acquire);
        xiaotai_board_camera_frame_t frame = {0};
        if (s_camera_adapter != NULL &&
            s_camera_adapter->acquire(s_camera_adapter->context, &frame) == ESP_OK) {
            bool complete = xiaotai_board_camera_frame_is_valid(&frame) &&
                            frame.format == XIAOTAI_CAMERA_FRAME_JPEG &&
                            frame.size >= 4 && frame.size <= VIDEO_MAX_BYTES &&
                            frame.data[0] == 0xff && frame.data[1] == 0xd8 &&
                            frame.data[frame.size - 2] == 0xff &&
                            frame.data[frame.size - 1] == 0xd9;
            if (complete && xSemaphoreTake(s_media_lock, pdMS_TO_TICKS(100)) == pdTRUE) {
                if (generation != 0U &&
                    generation == atomic_load_explicit(&s_generation, memory_order_acquire) &&
                    atomic_load_explicit(&s_active, memory_order_acquire) &&
                    starter_tirtc_video_ready() &&
                    starter_tirtc_send_buffer_used() < VIDEO_SEND_BUFFER_LIMIT &&
                    starter_tirtc_send_jpeg(timestamp_ms(), frame.data, frame.size) == 0) {
                    atomic_fetch_add_explicit(&s_video_sent, 1, memory_order_relaxed);
                }
                xSemaphoreGive(s_media_lock);
            }
            s_camera_adapter->release(s_camera_adapter->context, &frame);
        }
        vTaskDelay(pdMS_TO_TICKS(200));
    }
}

static void play_binding_clip(binding_digit_clip_t clip)
{
    if (clip.offset + clip.length >
        (size_t)(s_binding_pcm_end - s_binding_pcm_start)) return;
    size_t offset = 0;
    while (offset < clip.length &&
           atomic_load_explicit(&s_binding_valid, memory_order_acquire)) {
        size_t bytes = clip.length - offset;
        if (bytes > AUDIO_FRAMES_16K_20MS * 4U) {
            bytes = AUDIO_FRAMES_16K_20MS * 4U;
        }
        if (xSemaphoreTake(s_media_lock, pdMS_TO_TICKS(100)) != pdTRUE) break;
        size_t written = 0;
        if (!atomic_load_explicit(&s_active, memory_order_acquire) &&
            atomic_load_explicit(&s_binding_valid, memory_order_acquire)) {
            (void)s_audio_adapter->write_pcm(
                s_audio_adapter->context,
                (const int16_t *)(s_binding_pcm_start + clip.offset + offset),
                bytes / 4U, &written);
        }
        xSemaphoreGive(s_media_lock);
        if (written == 0U) break;
        offset += written;
    }
}

static void binding_speaker_task(void *argument)
{
    (void)argument;
    char code[BINDING_CODE_MAX + 1U];
    for (;;) {
        if (xQueueReceive(s_binding_queue, code, portMAX_DELAY) != pdTRUE) continue;
        for (unsigned repeat = 0; repeat < 3U &&
                                 atomic_load_explicit(&s_binding_valid, memory_order_acquire);
             ++repeat) {
            play_binding_clip(binding_prompt_clip);
            vTaskDelay(pdMS_TO_TICKS(200));
            for (size_t digit = 0; code[digit] != '\0' &&
                                   atomic_load_explicit(&s_binding_valid, memory_order_acquire);
                 ++digit) {
                play_binding_clip(binding_digit_clips[code[digit] - '0']);
                vTaskDelay(pdMS_TO_TICKS(150));
            }
            vTaskDelay(pdMS_TO_TICKS(700));
        }
        memset(code, 0, sizeof(code));
    }
}

static void key_probe_task(void *argument)
{
    (void)argument;
    atk_key_gesture_t gesture = {0};
    for (;;) {
        uint8_t pressed = 0;
        if (atk_board_keys(&pressed) == ESP_OK) {
            atk_key_poll_result_t result = atk_key_gesture_poll(
                &gesture, pressed, esp_timer_get_time() / 1000);
            if (result.stable_changed) {
                ESP_LOGI(TAG, "ATK KEY0..3 pressed mask=0x%02x", result.stable_mask);
            }
            if (result.action == ATK_KEY_ACTION_REPLAY_BINDING &&
                atomic_load_explicit(&s_binding_valid, memory_order_acquire) &&
                s_binding_queue != NULL &&
                xSemaphoreTake(s_media_lock, pdMS_TO_TICKS(20)) == pdTRUE) {
                (void)xQueueOverwrite(s_binding_queue, s_binding_code);
                xSemaphoreGive(s_media_lock);
            } else if (result.action == ATK_KEY_ACTION_RESET_WIFI ||
                       result.action == ATK_KEY_ACTION_RESET_BINDING) {
                starter_media_control_handler_t handler = NULL;
                void *user_data = NULL;
                if (xSemaphoreTake(s_media_lock, pdMS_TO_TICKS(20)) == pdTRUE) {
                    handler = s_control_handler;
                    user_data = s_control_user_data;
                    xSemaphoreGive(s_media_lock);
                }
                if (handler != NULL) {
                    handler(result.action == ATK_KEY_ACTION_RESET_WIFI
                                ? STARTER_MEDIA_CONTROL_WIFI_RESET
                                : STARTER_MEDIA_CONTROL_BINDING_RESET,
                            user_data);
                }
            }
        }
        vTaskDelay(pdMS_TO_TICKS(30));
    }
}

esp_err_t starter_media_init(void)
{
    if (atomic_load_explicit(&s_ready, memory_order_acquire)) {
        return ESP_OK;
    }
    /* 固定深度队列限制了弱网突发或扬声器阻塞时的内存占用。 */
    s_audio_rx_queue = xQueueCreate(AUDIO_RX_QUEUE_DEPTH, sizeof(audio_rx_item_t));
    if (s_audio_rx_queue == NULL) {
        return ESP_ERR_NO_MEM;
    }
    s_media_lock = xSemaphoreCreateMutex();
    if (s_media_lock == NULL) return ESP_ERR_NO_MEM;
    s_binding_queue = xQueueCreate(1, BINDING_CODE_MAX + 1U);
    if (s_binding_queue == NULL) return ESP_ERR_NO_MEM;
    s_audio_adapter = atk_board_audio_adapter();
    esp_err_t board_err = s_audio_adapter == NULL ? ESP_ERR_INVALID_STATE :
        s_audio_adapter->start(s_audio_adapter->context);
    if (board_err != ESP_OK) {
        ESP_LOGE(TAG, "board probe failed: %s", esp_err_to_name(board_err));
    }
    s_board_audio = board_err == ESP_OK &&
        s_audio_adapter->ready(s_audio_adapter->context);
    const xiaotai_board_audio_format_t audio_format =
        s_audio_adapter->format(s_audio_adapter->context);
    if (s_board_audio &&
        (!xiaotai_board_audio_format_is_valid(&audio_format) ||
         audio_format.sample_rate_hz != 16000U ||
         audio_format.capture_channels != 2U ||
         audio_format.playback_channels != 2U ||
         audio_format.capture_layout != XIAOTAI_CAPTURE_DUPLICATED_MIC)) {
        ESP_LOGE(TAG, "unsupported ATK capture layout");
        s_board_audio = false;
    }
    s_camera_adapter = atk_board_camera_adapter();
    s_board_camera = board_err == ESP_OK && s_camera_adapter != NULL &&
                     s_camera_adapter->ready(s_camera_adapter->context);
    if (xTaskCreate(audio_sink_task, "product_audio_rx", 4096, NULL, 6, NULL) !=
        pdPASS) {
        return ESP_ERR_NO_MEM;
    }
    if (s_board_audio && xTaskCreate(audio_capture_task, "product_audio_tx", 4096,
                                     NULL, 6, NULL) != pdPASS) return ESP_ERR_NO_MEM;
    if (s_board_audio && xTaskCreate(binding_speaker_task, "binding_voice", 4096,
                                     NULL, 5, NULL) != pdPASS) return ESP_ERR_NO_MEM;
    if (s_board_camera && xTaskCreate(video_capture_task, "product_video_tx", 4096,
                                      NULL, 5, NULL) != pdPASS) return ESP_ERR_NO_MEM;
    if (board_err == ESP_OK &&
        xTaskCreate(key_probe_task, "atk_keys", 2048, NULL, 3, NULL) != pdPASS) {
        return ESP_ERR_NO_MEM;
    }
    atomic_store_explicit(&s_ready, true, memory_order_release);
    ESP_LOGI(TAG, "ATK media probe audio=%d camera=%d", s_board_audio, s_board_camera);
    return ESP_OK;
}

esp_err_t starter_media_announce_binding_code(const char *code)
{
    if (!s_board_audio || s_binding_queue == NULL || code == NULL) {
        return ESP_ERR_NOT_SUPPORTED;
    }
    size_t length = strnlen(code, BINDING_CODE_MAX + 1U);
    if (length == 0U || length > BINDING_CODE_MAX) return ESP_ERR_INVALID_ARG;
    for (size_t i = 0; i < length; ++i) {
        if (code[i] < '0' || code[i] > '9') return ESP_ERR_INVALID_ARG;
    }
    if (xSemaphoreTake(s_media_lock, pdMS_TO_TICKS(100)) != pdTRUE) {
        return ESP_ERR_TIMEOUT;
    }
    memcpy(s_binding_code, code, length + 1U);
    atomic_store_explicit(&s_binding_valid, true, memory_order_release);
    BaseType_t queued = xQueueOverwrite(s_binding_queue, s_binding_code);
    xSemaphoreGive(s_media_lock);
    return queued == pdTRUE ? ESP_OK : ESP_FAIL;
}

void starter_media_clear_binding_code(void)
{
    if (s_media_lock == NULL) return;
    xSemaphoreTake(s_media_lock, portMAX_DELAY);
    atomic_store_explicit(&s_binding_valid, false, memory_order_release);
    memset(s_binding_code, 0, sizeof(s_binding_code));
    if (s_binding_queue != NULL) xQueueReset(s_binding_queue);
    xSemaphoreGive(s_media_lock);
}

void starter_media_set_control_handler(starter_media_control_handler_t handler,
                                       void *user_data)
{
    if (s_media_lock == NULL) return;
    xSemaphoreTake(s_media_lock, portMAX_DELAY);
    s_control_handler = handler;
    s_control_user_data = user_data;
    xSemaphoreGive(s_media_lock);
}

esp_err_t starter_media_start(starter_tirtc_mode_t mode, uint32_t generation)
{
    if (!atomic_load_explicit(&s_ready, memory_order_acquire) ||
        generation == 0U ||
        (mode != STARTER_TIRTC_H5 && mode != STARTER_TIRTC_AI)) {
        return ESP_ERR_INVALID_STATE;
    }
    /* AI requires a proven AEC playback reference before full-duplex activation. */
    if (mode == STARTER_TIRTC_AI || !s_board_audio || !s_board_camera) {
        ESP_LOGE(TAG, "media mode unavailable: mode=%d audio=%d camera=%d",
                 (int)mode, s_board_audio, s_board_camera);
        return ESP_ERR_NOT_SUPPORTED;
    }
    xSemaphoreTake(s_media_lock, portMAX_DELAY);
    /* 新会话不能播放上一代连接残留在队列中的数据。 */
    if (s_audio_rx_queue != NULL) {
        xQueueReset(s_audio_rx_queue);
    }
    atomic_store_explicit(&s_audio_sent, 0, memory_order_release);
    atomic_store_explicit(&s_video_sent, 0, memory_order_release);
    atomic_store_explicit(&s_audio_received, 0, memory_order_release);
    atomic_store_explicit(&s_audio_dropped, 0, memory_order_release);
    atomic_store_explicit(&s_mode, mode, memory_order_release);
    atomic_store_explicit(&s_generation, generation, memory_order_release);
    atomic_store_explicit(&s_active, true, memory_order_release);
    xSemaphoreGive(s_media_lock);
    ESP_LOGI(TAG,
             "ATK media started mode=%d generation=%lu",
             (int)mode,
             (unsigned long)generation);
    return ESP_OK;
}

void starter_media_stop(void)
{
    if (s_media_lock != NULL) xSemaphoreTake(s_media_lock, portMAX_DELAY);
    atomic_store_explicit(&s_active, false, memory_order_release);
    atomic_store_explicit(&s_generation, 0, memory_order_release);
    if (s_audio_rx_queue != NULL) {
        xQueueReset(s_audio_rx_queue);
    }
    if (s_media_lock != NULL) xSemaphoreGive(s_media_lock);
}

void starter_media_request_key_frame(uint32_t generation)
{
    if (generation == 0U ||
        generation != atomic_load_explicit(&s_generation, memory_order_acquire)) {
        return;
    }
    /* MJPEG is independently decodable on every frame. */
}

void starter_media_submit_audio(starter_tirtc_mode_t mode,
                                uint32_t generation,
                                const starter_tirtc_frame_t *frame,
                                const void *data)
{
    if (s_audio_rx_queue == NULL || frame == NULL || data == NULL ||
        frame->length == 0U || frame->length > AUDIO_RX_BYTES ||
        !same_session(mode, generation)) {
        atomic_fetch_add_explicit(&s_audio_dropped, 1, memory_order_relaxed);
        return;
    }
    /* data 由 SDK 持有，只在回调期间有效，所以必须在返回前复制。 */
    audio_rx_item_t item = {
        .mode = mode,
        .generation = generation,
        .frame = *frame,
    };
    memcpy(item.payload, data, frame->length);
    if (xQueueSend(s_audio_rx_queue, &item, 0) == pdTRUE) {
        atomic_fetch_add_explicit(&s_audio_received, 1, memory_order_relaxed);
    } else {
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
    };
}
