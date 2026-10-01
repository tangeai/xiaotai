/*
 * ESP-IDF rich-product sound echo cancellation boundary.
 *
 * Board adapters normalize capture into a compact mic/reference pair before
 * this module sees it. AEC runs at 16 kHz and the output is low-pass
 * decimated for the existing 8 kHz G.711/TiRTC transport boundary.
 */
#include "starter_aec.h"
#include "starter_audio_resampler.h"

#include <stdbool.h>
#include <stdlib.h>
#include <string.h>

#include "esp_aec.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_memory_utils.h"
#include "esp_timer.h"

#define AEC_ALIGNMENT_BYTES 16U
#define AEC_FILTER_LENGTH 4
#define AEC_MAX_FRAME_SAMPLES 1024U
#define AEC_CLIP_THRESHOLD 32700

typedef struct {
    aec_handle_t *handle;
    size_t frame_samples;
    int16_t *tdm;
    int16_t *mic;
    int16_t *reference;
    int16_t *clean;
    int16_t *output_8k;
    starter_audio_resampler_16k_to_8k_t resampler;
} starter_aec_context_t;

static const char *TAG = "starter_aec";
static starter_aec_context_t s_aec;

static void starter_aec_release(void)
{
    heap_caps_free(s_aec.tdm);
    heap_caps_free(s_aec.mic);
    heap_caps_free(s_aec.reference);
    heap_caps_free(s_aec.clean);
    heap_caps_free(s_aec.output_8k);
    if (s_aec.handle != NULL) {
        aec_destroy(s_aec.handle);
    }
    memset(&s_aec, 0, sizeof(s_aec));
}

static int16_t *allocate_samples(size_t samples, uint32_t caps)
{
    return heap_caps_aligned_calloc(AEC_ALIGNMENT_BYTES,
                                    samples,
                                    sizeof(int16_t),
                                    caps);
}

esp_err_t starter_aec_init(void)
{
    if (s_aec.handle != NULL) {
        return ESP_OK;
    }

    const size_t internal_before = heap_caps_get_free_size(
        MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    const size_t psram_before = heap_caps_get_free_size(MALLOC_CAP_SPIRAM);
    aec_config_t config = {
        .mic_num = 1,
        .ref_num = 1,
        .out_num = 1,
        .filter_length = AEC_FILTER_LENGTH,
        .sample_rate = STARTER_AEC_SAMPLE_RATE_HZ,
        .caps = MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT,
        .mode = AEC_MODE_FD_LOW_COST,
        .nlp_level = AEC_NLP_LEVEL_AGGR,
    };
    s_aec.handle = aec_create_from_config(&config);
    if (s_aec.handle == NULL) {
        ESP_LOGE(TAG, "ESP-SR AEC creation failed");
        return ESP_ERR_NO_MEM;
    }

    int chunk = aec_get_chunksize(s_aec.handle);
    if (chunk <= 0 || (chunk & 1) != 0 ||
        (size_t)chunk > AEC_MAX_FRAME_SAMPLES) {
        ESP_LOGE(TAG, "unsupported AEC chunk size: %d", chunk);
        starter_aec_release();
        return ESP_ERR_NOT_SUPPORTED;
    }
    s_aec.frame_samples = (size_t)chunk;

    /* I2S 目标放内部 SRAM；AEC 状态和计算帧优先放 8 MB PSRAM。 */
    s_aec.tdm = allocate_samples(s_aec.frame_samples * STARTER_AEC_CAPTURE_DMA_CHANNELS,
                                 MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    s_aec.mic = allocate_samples(s_aec.frame_samples,
                                 MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    s_aec.reference = allocate_samples(s_aec.frame_samples,
                                       MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    s_aec.clean = allocate_samples(s_aec.frame_samples,
                                   MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    s_aec.output_8k = allocate_samples(s_aec.frame_samples / 2U,
                                       MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (s_aec.tdm == NULL || s_aec.mic == NULL || s_aec.reference == NULL ||
        s_aec.clean == NULL || s_aec.output_8k == NULL ||
        !esp_ptr_internal(s_aec.tdm) || !esp_ptr_external_ram(s_aec.mic) ||
        !esp_ptr_external_ram(s_aec.reference) ||
        !esp_ptr_external_ram(s_aec.clean) ||
        !esp_ptr_external_ram(s_aec.output_8k)) {
        ESP_LOGE(TAG, "AEC aligned work-buffer allocation failed");
        starter_aec_release();
        return ESP_ERR_NO_MEM;
    }

    const size_t internal_after = heap_caps_get_free_size(
        MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    const size_t psram_after = heap_caps_get_free_size(MALLOC_CAP_SPIRAM);
    ESP_LOGI(TAG,
             "ESP-SR 2.4.7 AEC ready: mode=%s nlp=%s chunk=%u samples "
             "MIC1+MIC3 16k->8k, used internal=%u PSRAM=%u bytes",
             aec_get_mode_string(config.mode),
             aec_get_nlp_string(config.nlp_level),
             (unsigned)s_aec.frame_samples,
             (unsigned)(internal_before - internal_after),
             (unsigned)(psram_before - psram_after));
    return ESP_OK;
}

size_t starter_aec_capture_bytes(void)
{
    return s_aec.frame_samples * STARTER_AEC_CAPTURE_DMA_CHANNELS * sizeof(int16_t);
}

int16_t *starter_aec_capture_buffer(void)
{
    return s_aec.tdm;
}

size_t starter_aec_frame_samples(void)
{
    return s_aec.frame_samples;
}

esp_err_t starter_aec_process_capture(size_t capture_bytes,
                                      starter_aec_output_t *output)
{
    if (s_aec.handle == NULL || output == NULL ||
        capture_bytes != starter_aec_capture_bytes()) {
        return ESP_ERR_INVALID_ARG;
    }

    uint32_t mic_clipped = 0;
    uint32_t reference_clipped = 0;
    for (size_t i = 0; i < s_aec.frame_samples; ++i) {
        int16_t mic = s_aec.tdm[i * STARTER_AEC_CAPTURE_DMA_CHANNELS +
                                STARTER_AEC_MIC_SLOT];
        int16_t reference = s_aec.tdm[i * STARTER_AEC_CAPTURE_DMA_CHANNELS +
                                      STARTER_AEC_REFERENCE_SLOT];
        s_aec.mic[i] = mic;
        s_aec.reference[i] = reference;
        if (mic >= AEC_CLIP_THRESHOLD || mic <= -AEC_CLIP_THRESHOLD) {
            ++mic_clipped;
        }
        if (reference >= AEC_CLIP_THRESHOLD ||
            reference <= -AEC_CLIP_THRESHOLD) {
            ++reference_clipped;
        }
    }

    /* [DEBUG-wake49] Observe inputs before the opaque AEC call; never modify them. */
    int64_t measurement_start = esp_timer_get_time();
    starter_signal_level_t mic_level = starter_signal_measure(s_aec.mic, s_aec.frame_samples);
    starter_signal_level_t ref_level = starter_signal_measure(s_aec.reference, s_aec.frame_samples);
    uint32_t measurement_us = (uint32_t)(esp_timer_get_time() - measurement_start);
    aec_process(s_aec.handle, s_aec.mic, s_aec.reference, s_aec.clean);
    measurement_start = esp_timer_get_time();
    starter_signal_level_t clean_level = starter_signal_measure(s_aec.clean, s_aec.frame_samples);
    measurement_us += (uint32_t)(esp_timer_get_time() - measurement_start);

    /* Keep the filter timeline across AEC blocks so 16 kHz content cannot
     * alias into the 8 kHz A-law transport as a sharp remote voice. */
    const size_t output_samples = s_aec.frame_samples / 2U;
    if (starter_audio_resampler_16k_to_8k_process(&s_aec.resampler,
                                                   s_aec.clean,
                                                   s_aec.frame_samples,
                                                   s_aec.output_8k,
                                                   output_samples) != output_samples) {
        return ESP_FAIL;
    }

    *output = (starter_aec_output_t) {
        .pcm_16k = s_aec.clean,
        .samples_16k = s_aec.frame_samples,
        .pcm_8k = s_aec.output_8k,
        .samples = output_samples,
        .mic_clipped = mic_clipped,
        .reference_clipped = reference_clipped,
        .level = {mic_level, ref_level, clean_level},
        .measurement_us = measurement_us,
    };
    return ESP_OK;
}
