#include "board_adapter.h"
#include "board_config.h"
#include "board_power.h"
#include "board_audio_slots.h"
#include "starter_media_camera_policy.h"

#include <stdint.h>

#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "esp_camera.h"
#include "esp_log.h"
#include "driver/i2s_std.h"
#include "driver/i2s_tdm.h"
#include "esp_codec_dev.h"
#include "esp_codec_dev_defaults.h"
#include "es7210_adc.h"
#include "aw88298_dac.h"

static const char *TAG = "cores3_board";

static i2c_master_bus_handle_t s_i2c_bus;
static bool s_initialized;
static i2s_chan_handle_t s_i2s_rx;
static i2s_chan_handle_t s_i2s_tx;
/* One codec transaction owns paired RX/TX clocks; never enable separately. */
static const audio_codec_data_if_t *s_i2s_data_if;
static const audio_codec_ctrl_if_t *s_es7210_ctrl_if;
static const audio_codec_ctrl_if_t *s_amp_ctrl_if;
static const audio_codec_if_t *s_es7210_codec_if;
static const audio_codec_if_t *s_amp_codec_if;
static esp_codec_dev_handle_t s_microphone_dev;
static esp_codec_dev_handle_t s_speaker_dev;
static bool s_audio_ready;

esp_err_t szpi_board_init(void)
{
    if (s_initialized) return ESP_OK;
    if (!s_i2c_bus) {
        const i2c_master_bus_config_t cfg = {
            .i2c_port = BOARD_I2C_PORT, .sda_io_num = BOARD_I2C_SDA,
            .scl_io_num = BOARD_I2C_SCL, .clk_source = I2C_CLK_SRC_DEFAULT,
            .glitch_ignore_cnt = 7, .flags.enable_internal_pullup = true,
        };
        esp_err_t err = i2c_new_master_bus(&cfg, &s_i2c_bus);
        if (err != ESP_OK) return err;
    }
    esp_err_t err = cores3_board_init(s_i2c_bus);
    if (err == ESP_OK) s_initialized = true;
    return err;
}
i2c_master_bus_handle_t szpi_board_i2c_bus(void) { return s_i2c_bus; }
/* CS belongs to the SPI peripheral, not an expander on this board. */
esp_err_t szpi_board_select_lcd(bool selected)
{
    (void)selected;
    return s_initialized ? ESP_OK : ESP_ERR_INVALID_STATE;
}
esp_err_t szpi_board_power_camera(bool enabled)
{
    return cores3_board_camera_power(enabled);
}
esp_err_t szpi_board_power_amplifier(bool enabled)
{
    return szpi_board_audio_set_muted(!enabled);
}

esp_err_t szpi_board_camera_init(void)
{
    esp_err_t err = szpi_board_power_camera(true);
    if (err != ESP_OK) {
        return err;
    }
    const camera_config_t config = {
        .pin_pwdn = -1,
        .pin_reset = -1,
        .pin_xclk = CAMERA_XCLK,
        .pin_sccb_sda = -1,
        .pin_sccb_scl = BOARD_I2C_SCL,
        .pin_d7 = CAMERA_D7,
        .pin_d6 = CAMERA_D6,
        .pin_d5 = CAMERA_D5,
        .pin_d4 = CAMERA_D4,
        .pin_d3 = CAMERA_D3,
        .pin_d2 = CAMERA_D2,
        .pin_d1 = CAMERA_D1,
        .pin_d0 = CAMERA_D0,
        .pin_vsync = CAMERA_VSYNC,
        .pin_href = CAMERA_HREF,
        .pin_pclk = CAMERA_PCLK,
        .xclk_freq_hz = CAMERA_XCLK_HZ,
        .ledc_timer = LEDC_TIMER_0,
        .ledc_channel = LEDC_CHANNEL_1,
        .pixel_format = PIXFORMAT_RGB565,
        .frame_size = FRAMESIZE_QVGA,
        .jpeg_quality = 12,
        .fb_count = 2,
        .fb_location = CAMERA_FB_IN_PSRAM,
        .grab_mode = CAMERA_GRAB_WHEN_EMPTY,
        .sccb_i2c_port = BOARD_I2C_PORT,
    };
    err = esp_camera_init(&config);
    if (err != ESP_OK) {
        return err;
    }
    sensor_t *sensor = esp_camera_sensor_get();
    if (sensor == NULL || sensor->id.PID != GC0308_PID) {
        ESP_LOGE(TAG, "unsupported camera sensor PID=0x%04x",
                 sensor == NULL ? 0U : sensor->id.PID);
        (void)esp_camera_deinit();
        return ESP_ERR_NOT_FOUND;
    }
    ESP_LOGI(TAG, "accepted camera sensor %s, PID=0x%04x",
             starter_media_camera_sensor_name(sensor->id.PID), sensor->id.PID);
    (void)sensor->set_hmirror(sensor, 0);
    return ESP_OK;
}

static camera_fb_t *szpi_board_camera_get(void)
{
    return esp_camera_fb_get();
}

static void szpi_board_camera_return(camera_fb_t *frame)
{
    esp_camera_fb_return(frame);
}

static bool camera_adapter_ready(void *context)
{
    (void)context;
    return esp_camera_sensor_get() != NULL;
}

static int camera_adapter_start(void *context)
{
    (void)context;
    return szpi_board_camera_init();
}

static int camera_adapter_acquire(void *context,
                                  xiaotai_board_camera_frame_t *frame)
{
    (void)context;
    if (frame == NULL) return ESP_ERR_INVALID_ARG;
    camera_fb_t *native = szpi_board_camera_get();
    if (native == NULL) return ESP_ERR_TIMEOUT;
    *frame = (xiaotai_board_camera_frame_t) {
        .data = native->buf,
        .size = native->len,
        .width = native->width,
        .height = native->height,
        .format = native->format == PIXFORMAT_RGB565 ?
            XIAOTAI_CAMERA_FRAME_RGB565 : XIAOTAI_CAMERA_FRAME_UNKNOWN,
        .token = native,
    };
    return ESP_OK;
}

static void camera_adapter_release(void *context,
                                   xiaotai_board_camera_frame_t *frame)
{
    (void)context;
    if (frame == NULL || frame->token == NULL) return;
    szpi_board_camera_return((camera_fb_t *)frame->token);
    *frame = (xiaotai_board_camera_frame_t) {0};
}

static const xiaotai_board_camera_adapter_t s_camera_adapter = {
    .start = camera_adapter_start,
    .ready = camera_adapter_ready,
    .acquire = camera_adapter_acquire,
    .release = camera_adapter_release,
};

const xiaotai_board_camera_adapter_t *szpi_board_camera_adapter(void)
{
    return &s_camera_adapter;
}

static esp_err_t audio_i2s_init(void)
{
    /* Paired TX standard-I2S and RX four-slot TDM share BCLK/WS and 256 Fs MCLK. */
    i2s_chan_config_t audio_channel =
        I2S_CHANNEL_DEFAULT_CONFIG(I2S_AUDIO_PORT, I2S_ROLE_MASTER);
    audio_channel.auto_clear = true;
    audio_channel.dma_desc_num = AUDIO_I2S_DMA_DESC_NUM;
    audio_channel.dma_frame_num = AUDIO_I2S_DMA_FRAME_NUM;
    esp_err_t err = i2s_new_channel(&audio_channel, &s_i2s_tx, &s_i2s_rx);
    if (err != ESP_OK) {
        return err;
    }

    i2s_std_config_t tx_config = {
        .clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(AUDIO_HW_SAMPLE_RATE_HZ),
        .slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(
            I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_STEREO),
        .gpio_cfg = {
            .mclk = I2S_MCLK,
            .bclk = I2S_BCLK,
            .ws = I2S_WS,
            .dout = I2S_DAC_DOUT,
            .din = I2S_GPIO_UNUSED,
            .invert_flags = {
                .mclk_inv = false,
                .bclk_inv = false,
                .ws_inv = false,
            },
        },
    };
    tx_config.clk_cfg.mclk_multiple = I2S_MCLK_MULTIPLE_256;
    err = i2s_channel_init_std_mode(s_i2s_tx, &tx_config);
    if (err != ESP_OK) {
        return err;
    }

    i2s_tdm_config_t rx_config = {
        .clk_cfg = I2S_TDM_CLK_DEFAULT_CONFIG(AUDIO_HW_SAMPLE_RATE_HZ),
        .slot_cfg = I2S_TDM_PHILIPS_SLOT_DEFAULT_CONFIG(
            I2S_DATA_BIT_WIDTH_16BIT,
            I2S_SLOT_MODE_STEREO,
            I2S_TDM_SLOT0 | I2S_TDM_SLOT1 | I2S_TDM_SLOT2 | I2S_TDM_SLOT3),
        .gpio_cfg = {
            .mclk = I2S_MCLK,
            .bclk = I2S_BCLK,
            .ws = I2S_WS,
            .dout = I2S_GPIO_UNUSED,
            .din = I2S_ADC_DIN,
            .invert_flags = {
                .mclk_inv = false,
                .bclk_inv = false,
                .ws_inv = false,
            },
        },
    };
    rx_config.clk_cfg.mclk_multiple = I2S_MCLK_MULTIPLE_256;
    return i2s_channel_init_tdm_mode(s_i2s_rx, &rx_config);
}

static esp_err_t audio_amplifier_clock_init(void)
{
    if (s_amp_codec_if == NULL || s_amp_codec_if->get_reg == NULL ||
        s_amp_codec_if->set_reg == NULL) {
        return ESP_ERR_INVALID_STATE;
    }
    /* Four ADC slots share BCLK with two widened DAC slots: 64 bits per frame.
     * The codec's PCM-width setter selects 32fs, so change only I2SBCK after
     * both directions have opened. PCM samples remain 16-bit. */
    const int i2s_control = 0x06;
    const int bit_clock_mask = 0x30;
    const int bit_clock_64fs = 0x20;
    int before = 0;
    if (s_amp_codec_if->get_reg(s_amp_codec_if, i2s_control, &before) != ESP_CODEC_DEV_OK) {
        return ESP_FAIL;
    }
    int expected = (before & ~bit_clock_mask) | bit_clock_64fs;
    if (s_amp_codec_if->set_reg(s_amp_codec_if, i2s_control, expected) != ESP_CODEC_DEV_OK) {
        return ESP_FAIL;
    }
    int after = 0;
    if (s_amp_codec_if->get_reg(s_amp_codec_if, i2s_control, &after) != ESP_CODEC_DEV_OK ||
        after != expected) {
        ESP_LOGE(TAG, "amplifier shared clock readback failed");
        return ESP_FAIL;
    }
    ESP_LOGI(TAG, "amplifier shared clock: BCLK=64fs reg06=%04x->%04x", before, after);
    return ESP_OK;
}

static esp_err_t audio_codecs_init(void)
{
    audio_codec_i2s_cfg_t i2s_cfg = {
        .port = I2S_AUDIO_PORT,
        .rx_handle = s_i2s_rx,
        .tx_handle = s_i2s_tx,
    };
    s_i2s_data_if = audio_codec_new_i2s_data(&i2s_cfg);
    if (s_i2s_data_if == NULL) {
        return ESP_ERR_NO_MEM;
    }

    /* Codec addresses use the library's 8-bit wire representation. */
    audio_codec_i2c_cfg_t amp_i2c = {
        .port = BOARD_I2C_PORT,
        .addr = AW88298_CODEC_DEFAULT_ADDR,
        .bus_handle = s_i2c_bus,
        .clock_speed_hz = BOARD_I2C_HZ,
    };
    s_amp_ctrl_if = audio_codec_new_i2c_ctrl(&amp_i2c);
    if (s_amp_ctrl_if == NULL) {
        return ESP_ERR_NO_MEM;
    }
    aw88298_codec_cfg_t amp_cfg = {
        .ctrl_if = s_amp_ctrl_if,
        .reset_pin = -1,
    };
    s_amp_codec_if = aw88298_codec_new(&amp_cfg);
    if (s_amp_codec_if == NULL) {
        return ESP_ERR_NO_MEM;
    }
    esp_codec_dev_cfg_t speaker_cfg = {
        .dev_type = ESP_CODEC_DEV_TYPE_OUT,
        .codec_if = s_amp_codec_if,
        .data_if = s_i2s_data_if,
    };
    s_speaker_dev = esp_codec_dev_new(&speaker_cfg);
    if (s_speaker_dev == NULL) {
        return ESP_ERR_NO_MEM;
    }
    esp_codec_dev_sample_info_t speaker_format = {
        .sample_rate = AUDIO_HW_SAMPLE_RATE_HZ,
        .bits_per_sample = 16,
        .channel = 2,
    };
    if (esp_codec_dev_open(s_speaker_dev, &speaker_format) != ESP_CODEC_DEV_OK ||
        esp_codec_dev_set_out_vol(s_speaker_dev, 70) != ESP_CODEC_DEV_OK ||
        esp_codec_dev_set_out_mute(s_speaker_dev, true) != ESP_CODEC_DEV_OK) {
        return ESP_FAIL;
    }

    audio_codec_i2c_cfg_t es7210_i2c = {
        .port = BOARD_I2C_PORT,
        .addr = ES7210_CODEC_DEFAULT_ADDR,
        .bus_handle = s_i2c_bus,
        .clock_speed_hz = BOARD_I2C_HZ,
    };
    s_es7210_ctrl_if = audio_codec_new_i2c_ctrl(&es7210_i2c);
    if (s_es7210_ctrl_if == NULL) {
        return ESP_ERR_NO_MEM;
    }
    es7210_codec_cfg_t es7210_cfg = {
        .ctrl_if = s_es7210_ctrl_if,
        .master_mode = false,
        .mic_selected = ES7210_SEL_MIC1 | ES7210_SEL_MIC2 | ES7210_SEL_MIC3,
        .mclk_div = AUDIO_MCLK_MULTIPLE,
    };
    s_es7210_codec_if = es7210_codec_new(&es7210_cfg);
    if (s_es7210_codec_if == NULL) {
        return ESP_ERR_NO_MEM;
    }
    esp_codec_dev_cfg_t microphone_cfg = {
        .dev_type = ESP_CODEC_DEV_TYPE_IN,
        .codec_if = s_es7210_codec_if,
        .data_if = s_i2s_data_if,
    };
    s_microphone_dev = esp_codec_dev_new(&microphone_cfg);
    if (s_microphone_dev == NULL) {
        return ESP_ERR_NO_MEM;
    }
    /* Keep all wire slots; AFE repacks MIC1 + MIC3 reference to MR, MIC2 is not used by the shared single-microphone AEC. */
    esp_codec_dev_sample_info_t microphone_format = {
        .sample_rate = AUDIO_HW_SAMPLE_RATE_HZ,
        .bits_per_sample = 16,
        .channel = AUDIO_TDM_SLOTS,
        .channel_mask = ESP_CODEC_DEV_MAKE_CHANNEL_MASK(0) |
                        ESP_CODEC_DEV_MAKE_CHANNEL_MASK(1) |
                        ESP_CODEC_DEV_MAKE_CHANNEL_MASK(2) |
                        ESP_CODEC_DEV_MAKE_CHANNEL_MASK(3),
    };
    if (esp_codec_dev_open(s_microphone_dev, &microphone_format) != ESP_CODEC_DEV_OK ||
        esp_codec_dev_set_in_channel_gain(s_microphone_dev,
                                          ESP_CODEC_DEV_MAKE_CHANNEL_MASK(0),
                                          30.0f) != ESP_CODEC_DEV_OK ||
        esp_codec_dev_set_in_channel_gain(s_microphone_dev,
                                          ESP_CODEC_DEV_MAKE_CHANNEL_MASK(1),
                                          30.0f) != ESP_CODEC_DEV_OK ||
        esp_codec_dev_set_in_channel_gain(s_microphone_dev,
                                          ESP_CODEC_DEV_MAKE_CHANNEL_MASK(2),
                                          0.0f) != ESP_CODEC_DEV_OK) {
        return ESP_FAIL;
    }
    return audio_amplifier_clock_init();
}

esp_err_t szpi_board_audio_init(void)
{
    if (s_audio_ready) {
        return ESP_OK;
    }
    if (!s_initialized) {
        return ESP_ERR_INVALID_STATE;
    }
    esp_err_t err = audio_i2s_init();
    if (err == ESP_OK) {
        err = audio_codecs_init();
    }
    /* esp_codec_dev owns paired-channel enable; no direct I2S enable here. */
    if (err == ESP_OK) {
        s_audio_ready = true;
    }
    return err;
}

bool szpi_board_audio_ready(void)
{
    return s_audio_ready;
}

xiaotai_board_audio_format_t szpi_board_audio_format(void)
{
    return (xiaotai_board_audio_format_t){
        .sample_rate_hz = AUDIO_HW_SAMPLE_RATE_HZ,
        .capture_channels = 2U,
        .playback_channels = 2U,
        .capture_layout = XIAOTAI_CAPTURE_MIC_PLAYBACK_REFERENCE,
    };
}

esp_err_t szpi_board_audio_capture(int16_t *samples, size_t bytes)
{
    if (!s_audio_ready || samples == NULL) {
        return ESP_ERR_INVALID_STATE;
    }
    if (bytes % (2U * sizeof(int16_t))) return ESP_ERR_INVALID_ARG;
    int16_t wire[64 * AUDIO_TDM_SLOTS];
    size_t frames = bytes / (2U * sizeof(int16_t));
    for (size_t offset = 0; offset < frames;) {
        size_t count = frames - offset;
        if (count > 64) count = 64;
        if (esp_codec_dev_read(s_microphone_dev, wire,
                count * AUDIO_TDM_SLOTS * sizeof(int16_t)) != ESP_CODEC_DEV_OK)
            return ESP_FAIL;
        cores3_audio_unpack(samples + offset * 2, wire, count);
        offset += count;
    }
    return ESP_OK;
}

esp_err_t szpi_board_audio_play(const int16_t *samples, size_t bytes)
{
    if (!s_audio_ready || samples == NULL) {
        return ESP_ERR_INVALID_STATE;
    }
    return esp_codec_dev_write(s_speaker_dev, (void *)samples, bytes) ==
                   ESP_CODEC_DEV_OK ? ESP_OK : ESP_FAIL;
}

esp_err_t szpi_board_audio_set_volume(unsigned percent)
{
    if (!s_audio_ready || percent > 100U) {
        return ESP_ERR_INVALID_ARG;
    }
    return esp_codec_dev_set_out_vol(s_speaker_dev, percent) == ESP_CODEC_DEV_OK ?
               ESP_OK : ESP_FAIL;
}

esp_err_t szpi_board_audio_set_muted(bool muted)
{
    if (!s_audio_ready) {
        return ESP_ERR_INVALID_STATE;
    }
    return esp_codec_dev_set_out_mute(s_speaker_dev, muted) == ESP_CODEC_DEV_OK ?
               ESP_OK : ESP_FAIL;
}

static int audio_adapter_start(void *context)
{
    (void)context;
    return szpi_board_audio_init();
}

static bool audio_adapter_ready(void *context)
{
    (void)context;
    return szpi_board_audio_ready();
}

static xiaotai_board_audio_format_t audio_adapter_format(void *context)
{
    (void)context;
    return szpi_board_audio_format();
}

static int audio_adapter_read(void *context, int16_t *samples, size_t frames,
                              size_t *bytes_read)
{
    (void)context;
    xiaotai_board_audio_format_t format = szpi_board_audio_format();
    size_t bytes = xiaotai_board_audio_capture_bytes_for_frames(&format, frames);
    int rc = szpi_board_audio_capture(samples, bytes);
    if (bytes_read != NULL) *bytes_read = rc == ESP_OK ? bytes : 0U;
    return rc;
}

static int audio_adapter_write(void *context, const int16_t *samples,
                               size_t frames, size_t *bytes_written)
{
    (void)context;
    xiaotai_board_audio_format_t format = szpi_board_audio_format();
    size_t bytes = xiaotai_board_audio_playback_bytes_for_frames(&format, frames);
    int rc = szpi_board_audio_play(samples, bytes);
    if (bytes_written != NULL) *bytes_written = rc == ESP_OK ? bytes : 0U;
    return rc;
}

static int audio_adapter_set_capture_gain(void *context, unsigned gain)
{
    (void)context;
    if (gain > 36U || s_microphone_dev == NULL) return ESP_ERR_INVALID_ARG;
    /* MIC1 only: MIC3 is the physical playback reference. */
    return esp_codec_dev_set_in_channel_gain(s_microphone_dev,
        ESP_CODEC_DEV_MAKE_CHANNEL_MASK(0), (float)gain) == ESP_CODEC_DEV_OK
        ? ESP_OK : ESP_FAIL;
}

static int audio_adapter_set_volume(void *context, unsigned percent)
{
    (void)context;
    return szpi_board_audio_set_volume(percent);
}

static int audio_adapter_set_muted(void *context, bool muted)
{
    (void)context;
    return szpi_board_audio_set_muted(muted);
}

const xiaotai_board_audio_adapter_t *szpi_board_audio_adapter(void)
{
    static const xiaotai_board_audio_adapter_t adapter = {
        .start = audio_adapter_start,
        .ready = audio_adapter_ready,
        .format = audio_adapter_format,
        .read_pcm = audio_adapter_read,
        .write_pcm = audio_adapter_write,
        .set_capture_gain = audio_adapter_set_capture_gain,
        .set_volume = audio_adapter_set_volume,
        .set_muted = audio_adapter_set_muted,
    };
    return &adapter;
}
