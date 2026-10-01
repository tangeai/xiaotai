/*
 * Candidate ATK-DNESP32S3 V1.4 board adapter.
 * Pin values follow the local V1.4 schematic and openedv examples at c7434a3.
 * Runtime probes reject an absent expander/codec or an unlisted camera sensor.
 */
#include "atk_board.h"
#include "board_config.h"

#include <string.h>

#include "driver/gpio.h"
#include "driver/i2c_master.h"
#include "driver/i2s_std.h"
#include "esp_camera.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "sensor.h"

typedef struct {
    uint32_t mclk_hz;
    uint32_t sample_rate_hz;
    i2s_mclk_multiple_t mclk_multiple;
} atk_es8388_clock_t;

/* The ES8388 is clocked by the ESP32-S3 I2S master at 256 x 16 kHz. */
static const atk_es8388_clock_t s_es8388_clocks[] = {
    {4096000, 16000, I2S_MCLK_MULTIPLE_256},
};

static const char *TAG = "atk_board";
static i2c_master_bus_handle_t s_i2c_bus;
static i2c_master_dev_handle_t s_expander;
static i2c_master_dev_handle_t s_codec;
static i2s_chan_handle_t s_tx;
static i2s_chan_handle_t s_rx;
static uint16_t s_expander_output;
static bool s_audio_ready;
static bool s_camera_ready;
static int camera_adapter_start(void *context);

static esp_err_t expander_write(uint8_t reg, uint16_t value)
{
    uint8_t payload[] = {reg, (uint8_t)value, (uint8_t)(value >> 8)};
    return i2c_master_transmit(s_expander, payload, sizeof(payload), ATK_I2C_TIMEOUT_MS);
}

static esp_err_t expander_read(uint16_t *value)
{
    uint8_t reg = 0;
    uint8_t payload[2] = {0};
    esp_err_t err = i2c_master_transmit_receive(s_expander, &reg, 1, payload,
                                                 sizeof(payload), ATK_I2C_TIMEOUT_MS);
    if (err == ESP_OK) {
        *value = (uint16_t)payload[0] | ((uint16_t)payload[1] << 8);
    }
    return err;
}

static esp_err_t expander_set(uint16_t mask, bool high)
{
    uint16_t next = high ? (s_expander_output | mask) : (s_expander_output & ~mask);
    esp_err_t err = expander_write(2, next);
    if (err == ESP_OK) {
        s_expander_output = next;
    }
    return err;
}

static esp_err_t codec_write(uint8_t reg, uint8_t value)
{
    uint8_t payload[] = {reg, value};
    return i2c_master_transmit(s_codec, payload, sizeof(payload), ATK_I2C_TIMEOUT_MS);
}

static esp_err_t codec_init(void)
{
    /* ALIENTEK ES8388 ADC/DAC power and route setup from its recording example. */
    static const uint8_t setup[][2] = {
        {0x00, 0x80}, {0x00, 0x00}, {0x01, 0x58}, {0x01, 0x50},
        {0x02, 0xF3}, {0x02, 0xF0}, {0x03, 0x09}, {0x00, 0x06},
        {0x04, 0x00}, {0x08, 0x00}, {0x2B, 0x80}, {0x09, 0x88},
        {0x0C, 0x4C}, {0x0D, 0x02}, {0x10, 0x00}, {0x11, 0x00},
        {0x17, 0x18}, {0x18, 0x02}, {0x1A, 0x00}, {0x1B, 0x00},
        {0x27, 0xB8}, {0x2A, 0xB8},
    };
    for (size_t i = 0; i < sizeof(setup) / sizeof(setup[0]); ++i) {
        esp_err_t err = codec_write(setup[i][0], setup[i][1]);
        if (err != ESP_OK) {
            return err;
        }
        if (i == 1 || i == 5) {
            vTaskDelay(pdMS_TO_TICKS(100));
        }
    }
    /* Vendor helper equivalents: ADC+DAC on, MIC input 1, both DAC routes,
     * maximum MIC PGA, stereo ALC(4,4), Philips I2S 16 bit, moderate volume. */
    const uint8_t tail[][2] = {
        {0x02, 0x00}, {0x0A, 0x00}, {0x09, 0x88},
        {0x12, 0xE4}, {0x04, 0x3C}, {0x17, 0x18},
        {0x2E, 0x14}, {0x2F, 0x14},
        {0x30, 0x14}, {0x31, 0x14},
    };
    for (size_t i = 0; i < sizeof(tail) / sizeof(tail[0]); ++i) {
        esp_err_t err = codec_write(tail[i][0], tail[i][1]);
        if (err != ESP_OK) {
            return err;
        }
    }
    return ESP_OK;
}

static esp_err_t i2s_init(void)
{
    i2s_chan_config_t channel = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_0, I2S_ROLE_MASTER);
    esp_err_t err = i2s_new_channel(&channel, &s_tx, &s_rx);
    if (err != ESP_OK) {
        return err;
    }
    i2s_std_config_t standard = {
        .clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(s_es8388_clocks[0].sample_rate_hz),
        .slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_16BIT,
                                                        I2S_SLOT_MODE_STEREO),
        .gpio_cfg = {
            .mclk = ATK_I2S_MCLK,
            .bclk = ATK_I2S_BCLK,
            .ws = ATK_I2S_WS,
            .dout = ATK_I2S_DOUT,
            .din = ATK_I2S_DIN,
        },
    };
    standard.clk_cfg.mclk_multiple = s_es8388_clocks[0].mclk_multiple;
    err = i2s_channel_init_std_mode(s_tx, &standard);
    if (err == ESP_OK) err = i2s_channel_init_std_mode(s_rx, &standard);
    if (err == ESP_OK) err = i2s_channel_enable(s_tx);
    if (err == ESP_OK) err = i2s_channel_enable(s_rx);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "I2S setup failed: %s", esp_err_to_name(err));
        if (s_rx != NULL) {
            i2s_del_channel(s_rx);
            s_rx = NULL;
        }
        if (s_tx != NULL) {
            i2s_del_channel(s_tx);
            s_tx = NULL;
        }
    }
    return err;
}

static esp_err_t camera_init(void)
{
    esp_err_t err = expander_set(ATK_XL9555_OV_PWDN, false);
    if (err != ESP_OK) return err;
    err = expander_set(ATK_XL9555_OV_RESET, false);
    if (err != ESP_OK) return err;
    vTaskDelay(pdMS_TO_TICKS(20));
    err = expander_set(ATK_XL9555_OV_RESET, true);
    if (err != ESP_OK) return err;
    vTaskDelay(pdMS_TO_TICKS(20));

    camera_config_t config = {
        .pin_pwdn = -1, .pin_reset = -1, .pin_xclk = -1,
        .pin_sccb_sda = ATK_CAMERA_SCCB_SDA,
        .pin_sccb_scl = ATK_CAMERA_SCCB_SCL,
        .pin_d0 = ATK_CAMERA_D0, .pin_d1 = ATK_CAMERA_D1,
        .pin_d2 = ATK_CAMERA_D2, .pin_d3 = ATK_CAMERA_D3,
        .pin_d4 = ATK_CAMERA_D4, .pin_d5 = ATK_CAMERA_D5,
        .pin_d6 = ATK_CAMERA_D6, .pin_d7 = ATK_CAMERA_D7,
        .pin_vsync = ATK_CAMERA_VSYNC,
        .pin_href = ATK_CAMERA_HREF,
        .pin_pclk = ATK_CAMERA_PCLK,
        .xclk_freq_hz = ATK_CAMERA_XCLK_HZ,
        .ledc_timer = LEDC_TIMER_0, .ledc_channel = LEDC_CHANNEL_0,
        .pixel_format = PIXFORMAT_JPEG,
        .frame_size = FRAMESIZE_QVGA,
        .jpeg_quality = 15,
        .fb_count = 2,
        .fb_location = CAMERA_FB_IN_PSRAM,
        .grab_mode = CAMERA_GRAB_WHEN_EMPTY,
    };
    err = esp_camera_init(&config);
    if (err != ESP_OK) {
        return err;
    }
    sensor_t *sensor = esp_camera_sensor_get();
    if (sensor == NULL ||
        (sensor->id.PID != OV2640_PID && sensor->id.PID != OV5640_PID)) {
        ESP_LOGE(TAG, "camera sensor PID not in OV2640/OV5640 allowlist: 0x%04x",
                 sensor != NULL ? sensor->id.PID : 0);
        esp_camera_deinit();
        return ESP_ERR_NOT_SUPPORTED;
    }
    ESP_LOGI(TAG, "camera PID=0x%04x JPEG QVGA", sensor->id.PID);
    return ESP_OK;
}

esp_err_t atk_board_init(void)
{
    i2c_master_bus_config_t bus = {
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .i2c_port = I2C_NUM_0,
        .sda_io_num = ATK_I2C_SDA,
        .scl_io_num = ATK_I2C_SCL,
        .glitch_ignore_cnt = 7,
        .flags.enable_internal_pullup = true,
    };
    esp_err_t err = i2c_new_master_bus(&bus, &s_i2c_bus);
    if (err != ESP_OK) return err;
    i2c_device_config_t expander = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = ATK_XL9555_ADDR,
        .scl_speed_hz = 100000,
    };
    err = i2c_master_bus_add_device(s_i2c_bus, &expander, &s_expander);
    if (err != ESP_OK) return err;
    i2c_device_config_t codec = expander;
    codec.device_address = ATK_ES8388_ADDR;
    err = i2c_master_bus_add_device(s_i2c_bus, &codec, &s_codec);
    if (err != ESP_OK) return err;
    err = i2c_master_probe(s_i2c_bus, ATK_XL9555_ADDR, ATK_I2C_TIMEOUT_MS);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "XL9555 missing at 0x20: %s", esp_err_to_name(err));
        return err;
    }
    s_expander_output = ATK_XL9555_SPK_EN | ATK_XL9555_BEEP |
                        ATK_XL9555_OV_RESET;
    err = expander_write(2, s_expander_output);
    if (err == ESP_OK) err = expander_write(6, 0xF003U);
    if (err != ESP_OK) return err;

    if (i2c_master_probe(s_i2c_bus, ATK_ES8388_ADDR, ATK_I2C_TIMEOUT_MS) == ESP_OK) {
        err = codec_init();
        if (err == ESP_OK) err = i2s_init();
        if (err == ESP_OK) {
            err = expander_set(ATK_XL9555_SPK_EN, false);
        }
        if (err == ESP_OK) {
            s_audio_ready = true;
            ESP_LOGI(TAG, "ES8388/I2S0 ready at 16 kHz stereo");
        } else {
            ESP_LOGE(TAG, "audio unavailable: %s", esp_err_to_name(err));
        }
    } else {
        ESP_LOGE(TAG, "ES8388 missing at 0x10");
    }
    err = camera_adapter_start(NULL);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "camera unavailable: %s", esp_err_to_name(err));
    }
    return ESP_OK;
}

bool atk_board_audio_ready(void) { return s_audio_ready; }
bool atk_board_camera_ready(void) { return s_camera_ready; }

xiaotai_board_audio_format_t atk_board_audio_format(void)
{
    return (xiaotai_board_audio_format_t){
        .sample_rate_hz = 16000U,
        .capture_channels = 2U,
        .playback_channels = 2U,
        .capture_layout = XIAOTAI_CAPTURE_DUPLICATED_MIC,
    };
}

esp_err_t atk_board_audio_read(int16_t *stereo, size_t frames, size_t *bytes_read)
{
    if (!s_audio_ready || stereo == NULL || bytes_read == NULL) return ESP_ERR_INVALID_STATE;
    xiaotai_board_audio_format_t format = atk_board_audio_format();
    return i2s_channel_read(s_rx, stereo,
                            xiaotai_board_audio_capture_bytes_for_frames(&format,
                                                                         frames),
                            bytes_read, pdMS_TO_TICKS(40));
}

esp_err_t atk_board_audio_write(const int16_t *stereo, size_t frames, size_t *bytes_written)
{
    if (!s_audio_ready || stereo == NULL || bytes_written == NULL) return ESP_ERR_INVALID_STATE;
    xiaotai_board_audio_format_t format = atk_board_audio_format();
    return i2s_channel_write(s_tx, stereo,
                             xiaotai_board_audio_playback_bytes_for_frames(&format,
                                                                           frames),
                             bytes_written, pdMS_TO_TICKS(40));
}

static camera_fb_t *atk_board_camera_get(void)
{
    return s_camera_ready ? esp_camera_fb_get() : NULL;
}

static void atk_board_camera_return(camera_fb_t *frame)
{
    if (frame != NULL) esp_camera_fb_return(frame);
}

static int camera_adapter_start(void *context)
{
    (void)context;
    if (s_camera_ready) return ESP_OK;
    esp_err_t err = camera_init();
    s_camera_ready = err == ESP_OK;
    return err;
}

static bool camera_adapter_ready(void *context)
{
    (void)context;
    return s_camera_ready;
}

static int camera_adapter_acquire(void *context,
                                  xiaotai_board_camera_frame_t *frame)
{
    (void)context;
    if (frame == NULL) return ESP_ERR_INVALID_ARG;
    camera_fb_t *native = atk_board_camera_get();
    if (native == NULL) return ESP_ERR_TIMEOUT;
    *frame = (xiaotai_board_camera_frame_t) {
        .data = native->buf,
        .size = native->len,
        .width = native->width,
        .height = native->height,
        .format = native->format == PIXFORMAT_JPEG ?
            XIAOTAI_CAMERA_FRAME_JPEG : XIAOTAI_CAMERA_FRAME_UNKNOWN,
        .token = native,
    };
    return ESP_OK;
}

static void camera_adapter_release(void *context,
                                   xiaotai_board_camera_frame_t *frame)
{
    (void)context;
    if (frame == NULL || frame->token == NULL) return;
    atk_board_camera_return((camera_fb_t *)frame->token);
    *frame = (xiaotai_board_camera_frame_t) {0};
}

static const xiaotai_board_camera_adapter_t s_camera_adapter = {
    .start = camera_adapter_start,
    .ready = camera_adapter_ready,
    .acquire = camera_adapter_acquire,
    .release = camera_adapter_release,
};

const xiaotai_board_camera_adapter_t *atk_board_camera_adapter(void)
{
    return &s_camera_adapter;
}

esp_err_t atk_board_keys(uint8_t *pressed_mask)
{
    if (pressed_mask == NULL || s_expander == NULL) return ESP_ERR_INVALID_ARG;
    uint16_t inputs = 0;
    esp_err_t err = expander_read(&inputs);
    if (err == ESP_OK) {
        *pressed_mask = (uint8_t)(((~inputs) & ATK_XL9555_KEY_MASK) >> 12);
    }
    return err;
}

static int audio_adapter_start(void *context)
{
    (void)context;
    return atk_board_init();
}

static bool audio_adapter_ready(void *context)
{
    (void)context;
    return atk_board_audio_ready();
}

static xiaotai_board_audio_format_t audio_adapter_format(void *context)
{
    (void)context;
    return atk_board_audio_format();
}

static int audio_adapter_read(void *context, int16_t *samples, size_t frames,
                              size_t *bytes_read)
{
    (void)context;
    return atk_board_audio_read(samples, frames, bytes_read);
}

static int audio_adapter_write(void *context, const int16_t *samples,
                               size_t frames, size_t *bytes_written)
{
    (void)context;
    return atk_board_audio_write(samples, frames, bytes_written);
}

const xiaotai_board_audio_adapter_t *atk_board_audio_adapter(void)
{
    static const xiaotai_board_audio_adapter_t adapter = {
        .start = audio_adapter_start,
        .ready = audio_adapter_ready,
        .format = audio_adapter_format,
        .read_pcm = audio_adapter_read,
        .write_pcm = audio_adapter_write,
    };
    return &adapter;
}
