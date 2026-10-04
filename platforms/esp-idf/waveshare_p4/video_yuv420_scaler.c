#include "video_yuv420_scaler.h"

#include <stdlib.h>
#include <string.h>

#include "driver/ppa.h"
#include "esp_cache.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "sdkconfig.h"

#include "app_memory_policy.h"

static const char *TAG = "video_scaler";

#define VIDEO_YUV420_SCALE_DENOMINATOR 16U

#if CONFIG_CACHE_L2_CACHE_LINE_SIZE > CONFIG_CACHE_L1_CACHE_LINE_SIZE
#define VIDEO_YUV420_CACHE_LINE_SIZE CONFIG_CACHE_L2_CACHE_LINE_SIZE
#else
#define VIDEO_YUV420_CACHE_LINE_SIZE CONFIG_CACHE_L1_CACHE_LINE_SIZE
#endif

struct video_yuv420_scaler {
    ppa_client_handle_t ppa_client;
    uint8_t *output_buffer;
    size_t output_buffer_size;
    size_t output_data_len;
    video_yuv420_scaler_config_t config;
    uint16_t crop_width;
    uint16_t crop_height;
    uint16_t crop_x;
    uint16_t crop_y;
    uint16_t output_x;
    uint16_t output_y;
    uint16_t render_width;
    uint16_t render_height;
    uint8_t scale_step;
};

static size_t video_yuv420_data_size(uint16_t width, uint16_t height)
{
    return ((size_t)width * (size_t)height * 3U) / 2U;
}

static size_t video_yuv420_align_up(size_t value, size_t alignment)
{
    return (value + alignment - 1U) & ~(alignment - 1U);
}

/* PPA YUV420 is stored as alternating OUYY/EVYY rows: one U or V byte
 * followed by the two luma samples it belongs to.  It is not planar I420.
 * Limited-range black is Y=16, U=128, V=128, hence the same three-byte
 * pattern covers both row types. */
static void video_yuv420_fill_black_ouyy_evyy(uint8_t *buffer,
                                              uint16_t width,
                                              uint16_t height)
{
    const size_t data_len = video_yuv420_data_size(width, height);
    for (size_t offset = 0U; offset < data_len; offset += 3U) {
        buffer[offset] = 128U;
        buffer[offset + 1U] = 16U;
        buffer[offset + 2U] = 16U;
    }
}

static bool video_yuv420_config_valid(const video_yuv420_scaler_config_t *config)
{
    return config != NULL &&
           config->input_width > 0U && config->input_height > 0U &&
           config->output_width > 0U && config->output_height > 0U &&
           (config->input_width & 1U) == 0U && (config->input_height & 1U) == 0U &&
           (config->output_width & 1U) == 0U && (config->output_height & 1U) == 0U &&
           (config->rotate_ccw90 ? config->output_height : config->output_width) <= config->input_width &&
           (config->rotate_ccw90 ? config->output_width : config->output_height) <= config->input_height;
}

static bool video_yuv420_select_geometry(const video_yuv420_scaler_config_t *config,
                                         uint16_t *crop_width,
                                         uint16_t *crop_height,
                                         uint16_t *crop_x,
                                         uint16_t *crop_y,
                                         uint16_t *output_x,
                                         uint16_t *output_y,
                                         uint8_t *scale_step)
{
    *output_x = 0U;
    *output_y = 0U;

    if (config->fit_contain) {
        /* Keep the complete sensor image. Pick the largest exact PPA step
         * whose rotated result fits inside the encoded canvas. */
        for (uint8_t step = VIDEO_YUV420_SCALE_DENOMINATOR; step >= 2U; step -= 2U) {
            uint32_t width_numerator = (uint32_t)config->input_width * step;
            uint32_t height_numerator = (uint32_t)config->input_height * step;
            if ((width_numerator % VIDEO_YUV420_SCALE_DENOMINATOR) != 0U ||
                (height_numerator % VIDEO_YUV420_SCALE_DENOMINATOR) != 0U) {
                continue;
            }

            uint32_t sensor_width = width_numerator / VIDEO_YUV420_SCALE_DENOMINATOR;
            uint32_t sensor_height = height_numerator / VIDEO_YUV420_SCALE_DENOMINATOR;
            uint32_t rendered_width = config->rotate_ccw90 ? sensor_height : sensor_width;
            uint32_t rendered_height = config->rotate_ccw90 ? sensor_width : sensor_height;
            if (rendered_width > config->output_width || rendered_height > config->output_height ||
                (rendered_width & 1U) != 0U || (rendered_height & 1U) != 0U) {
                continue;
            }

            *crop_width = config->input_width;
            *crop_height = config->input_height;
            *crop_x = 0U;
            *crop_y = 0U;
            *output_x = (uint16_t)(((uint32_t)config->output_width - rendered_width) / 2U) & ~1U;
            *output_y = (uint16_t)(((uint32_t)config->output_height - rendered_height) / 2U) & ~1U;
            *scale_step = step;
            return true;
        }
        return false;
    }

    /* Select the crop in SENSOR axes, before PPA exchanges the output axes. */
    const uint32_t scaled_width_numerator = (uint32_t)(config->rotate_ccw90 ? config->output_height : config->output_width) * VIDEO_YUV420_SCALE_DENOMINATOR;
    const uint32_t scaled_height_numerator = (uint32_t)(config->rotate_ccw90 ? config->output_width : config->output_height) * VIDEO_YUV420_SCALE_DENOMINATOR;

    /* PPA YUV420 masks the fractional scale bit 0, so only even 1/16 steps are exact. */
    for (uint8_t step = 2U; step <= VIDEO_YUV420_SCALE_DENOMINATOR; step += 2U) {
        if ((scaled_width_numerator % step) != 0U || (scaled_height_numerator % step) != 0U) {
            continue;
        }

        uint32_t candidate_width = scaled_width_numerator / step;
        uint32_t candidate_height = scaled_height_numerator / step;
        if (candidate_width > config->input_width || candidate_height > config->input_height ||
            (candidate_width & 1U) != 0U || (candidate_height & 1U) != 0U) {
            continue;
        }

        uint32_t x = ((uint32_t)config->input_width - candidate_width) / 2U;
        uint32_t y = ((uint32_t)config->input_height - candidate_height) / 2U;
        x &= ~1U;
        y &= ~1U;

        *crop_width = (uint16_t)candidate_width;
        *crop_height = (uint16_t)candidate_height;
        *crop_x = (uint16_t)x;
        *crop_y = (uint16_t)y;
        *scale_step = step;
        return true;
    }

    return false;
}

esp_err_t video_yuv420_scaler_create(const video_yuv420_scaler_config_t *config,
                                     video_yuv420_scaler_handle_t *out_handle)
{
    if (!video_yuv420_config_valid(config) || out_handle == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    *out_handle = NULL;

    struct video_yuv420_scaler *scaler =
        heap_caps_calloc(1, sizeof(*scaler), MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    if (scaler == NULL) {
        return ESP_ERR_NO_MEM;
    }
    scaler->config = *config;

    if (!video_yuv420_select_geometry(config,
                                      &scaler->crop_width,
                                      &scaler->crop_height,
                                      &scaler->crop_x,
                                      &scaler->crop_y,
                                      &scaler->output_x,
                                      &scaler->output_y,
                                      &scaler->scale_step)) {
        free(scaler);
        return ESP_ERR_NOT_SUPPORTED;
    }

    scaler->output_data_len = video_yuv420_data_size(config->output_width, config->output_height);
    scaler->output_buffer_size =
        video_yuv420_align_up(scaler->output_data_len, VIDEO_YUV420_CACHE_LINE_SIZE);
    scaler->output_buffer =
        app_memory_aligned_alloc_psram(VIDEO_YUV420_CACHE_LINE_SIZE,
                                       scaler->output_buffer_size,
                                       MALLOC_CAP_DMA);
    if (scaler->output_buffer == NULL) {
        ESP_LOGE(TAG,
                 "YUV420 scaler output allocation failed: size=%u psram_largest=%u",
                 (unsigned)scaler->output_buffer_size,
                 (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
        free(scaler);
        return ESP_ERR_NO_MEM;
    }

    uint16_t scaled_sensor_width =
        (uint16_t)(((uint32_t)scaler->crop_width * scaler->scale_step) /
                   VIDEO_YUV420_SCALE_DENOMINATOR);
    uint16_t scaled_sensor_height =
        (uint16_t)(((uint32_t)scaler->crop_height * scaler->scale_step) /
                   VIDEO_YUV420_SCALE_DENOMINATOR);
    scaler->render_width = config->rotate_ccw90 ? scaled_sensor_height : scaled_sensor_width;
    scaler->render_height = config->rotate_ccw90 ? scaled_sensor_width : scaled_sensor_height;

    if (scaler->render_width != config->output_width ||
        scaler->render_height != config->output_height) {
        video_yuv420_fill_black_ouyy_evyy(scaler->output_buffer,
                                          config->output_width,
                                          config->output_height);
        esp_err_t sync_ret = esp_cache_msync(scaler->output_buffer,
                                             scaler->output_buffer_size,
                                             ESP_CACHE_MSYNC_FLAG_DIR_C2M);
        if (sync_ret != ESP_OK) {
            ESP_LOGE(TAG, "YUV420 letterbox background sync failed: %s",
                     esp_err_to_name(sync_ret));
            free(scaler->output_buffer);
            free(scaler);
            return sync_ret;
        }
    }

    ppa_client_config_t ppa_config = {
        .oper_type = PPA_OPERATION_SRM,
        .max_pending_trans_num = 1,
        .data_burst_length = PPA_DATA_BURST_LENGTH_128,
    };
    esp_err_t ret = ppa_register_client(&ppa_config, &scaler->ppa_client);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "PPA scaler client registration failed: %s", esp_err_to_name(ret));
        free(scaler->output_buffer);
        free(scaler);
        return ret;
    }

    ESP_LOGI(TAG,
             "YUV420 scaler ready: input=%ux%u crop=%ux%u+%u+%u scale=%u/16 render=%ux%u+%u+%u output=%ux%u fit=%s buffer=%u",
             config->input_width,
             config->input_height,
             scaler->crop_width,
             scaler->crop_height,
             scaler->crop_x,
             scaler->crop_y,
             scaler->scale_step,
             scaler->render_width,
             scaler->render_height,
             scaler->output_x,
             scaler->output_y,
             config->output_width,
             config->output_height,
             config->fit_contain ? "contain" : "cover",
             (unsigned)scaler->output_buffer_size);

    *out_handle = scaler;
    return ESP_OK;
}

bool video_yuv420_scaler_matches(video_yuv420_scaler_handle_t handle,
                                 const video_yuv420_scaler_config_t *config)
{
    if (handle == NULL || config == NULL) {
        return false;
    }

    return handle->config.input_width == config->input_width &&
           handle->config.input_height == config->input_height &&
           handle->config.output_width == config->output_width &&
           handle->config.output_height == config->output_height &&
           handle->config.rotate_ccw90 == config->rotate_ccw90 &&
           handle->config.fit_contain == config->fit_contain;
}

esp_err_t video_yuv420_scaler_warmup(video_yuv420_scaler_handle_t handle)
{
    if (handle == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    size_t input_data_len =
        video_yuv420_data_size(handle->config.input_width, handle->config.input_height);
    size_t input_buffer_size =
        video_yuv420_align_up(input_data_len, VIDEO_YUV420_CACHE_LINE_SIZE);
    uint8_t *input = app_memory_aligned_alloc_psram(VIDEO_YUV420_CACHE_LINE_SIZE,
                                                    input_buffer_size,
                                                    MALLOC_CAP_DMA);
    if (input == NULL) {
        ESP_LOGE(TAG,
                 "YUV420 scaler warmup input allocation failed: size=%u psram_largest=%u",
                 (unsigned)input_buffer_size,
                 (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_SPIRAM |
                                                             MALLOC_CAP_8BIT));
        return ESP_ERR_NO_MEM;
    }

    size_t luma_len = (size_t)handle->config.input_width * handle->config.input_height;
    memset(input, 16, luma_len);
    memset(input + luma_len, 128, input_data_len - luma_len);

    const uint8_t *output = NULL;
    size_t output_len = 0;
    int64_t started_us = esp_timer_get_time();
    esp_err_t ret = video_yuv420_scaler_process(handle,
                                                input,
                                                input_data_len,
                                                &output,
                                                &output_len);
    int64_t elapsed_us = esp_timer_get_time() - started_us;
    free(input);

    ESP_LOGI(TAG,
             "YUV420 scaler warmup: input=%ux%u output=%ux%u elapsed=%lldus ret=%s",
             handle->config.input_width,
             handle->config.input_height,
             handle->config.output_width,
             handle->config.output_height,
             (long long)elapsed_us,
             esp_err_to_name(ret));
    return ret;
}

esp_err_t video_yuv420_scaler_process(video_yuv420_scaler_handle_t handle,
                                      const uint8_t *input,
                                      size_t input_len,
                                      const uint8_t **output,
                                      size_t *output_len)
{
    if (handle == NULL || input == NULL || output == NULL || output_len == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    size_t expected_input_len =
        video_yuv420_data_size(handle->config.input_width, handle->config.input_height);
    if (input_len < expected_input_len) {
        return ESP_ERR_INVALID_SIZE;
    }

    const float scale = (float)handle->scale_step / (float)VIDEO_YUV420_SCALE_DENOMINATOR;
    ppa_srm_oper_config_t operation = {
        .in.buffer = input,
        .in.pic_w = handle->config.input_width,
        .in.pic_h = handle->config.input_height,
        .in.block_w = handle->crop_width,
        .in.block_h = handle->crop_height,
        .in.block_offset_x = handle->crop_x,
        .in.block_offset_y = handle->crop_y,
        .in.srm_cm = PPA_SRM_COLOR_MODE_YUV420,

        .out.buffer = handle->output_buffer,
        .out.buffer_size = handle->output_buffer_size,
        .out.pic_w = handle->config.output_width,
        .out.pic_h = handle->config.output_height,
        .out.block_offset_x = handle->output_x,
        .out.block_offset_y = handle->output_y,
        .out.srm_cm = PPA_SRM_COLOR_MODE_YUV420,

        /* PPA positive angles are counterclockwise, not clockwise. */
        .rotation_angle = handle->config.rotate_ccw90 ? PPA_SRM_ROTATION_ANGLE_90 : PPA_SRM_ROTATION_ANGLE_0,
        .scale_x = scale,
        .scale_y = scale,
        .mode = PPA_TRANS_MODE_BLOCKING,
    };

    esp_err_t ret = ppa_do_scale_rotate_mirror(handle->ppa_client, &operation);
    if (ret != ESP_OK) {
        return ret;
    }

    /* PPA owns transaction cache synchronization. For contain mode the CPU
     * initialized the untouched letterbox area once during creation. */
    *output = handle->output_buffer;
    *output_len = handle->output_data_len;
    return ESP_OK;
}

void video_yuv420_scaler_destroy(video_yuv420_scaler_handle_t handle)
{
    if (handle == NULL) {
        return;
    }

    if (handle->ppa_client != NULL) {
        esp_err_t ret = ppa_unregister_client(handle->ppa_client);
        if (ret != ESP_OK) {
            ESP_LOGW(TAG, "PPA scaler client release failed: %s", esp_err_to_name(ret));
        }
    }
    free(handle->output_buffer);
    free(handle);
}
