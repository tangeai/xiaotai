#include "board_audio.h"
#include "board_config.h"

#include <driver/gpio.h>
#include <os/os.h>

#include "audio_play.h"
#include "audio_record.h"
#include "gpio_driver.h"
#include "xiaotai_log.h"

#define TAG "xiaotai_board_audio"
#define AMPLIFIER_POWER_SETTLE_MS 20U
#define AMPLIFIER_MUTE_SETTLE_MS 5U
#define BOARD_AUDIO_DEFAULT_SAMPLE_RATE_HZ 8000U
#define BOARD_AUDIO_DEFAULT_FRAME_SAMPLES 320U
#define BOARD_AUDIO_DEFAULT_ADC_GAIN 0x2dU
#define BOARD_AUDIO_MAX_ADC_GAIN 0x3fU

static bool s_amplifier_prepared;
static audio_record_t *s_record;
static audio_play_t *s_play;
static audio_play_t *s_prompt_play;
static bool s_audio_ready;
static uint32_t s_sample_rate_hz = BOARD_AUDIO_DEFAULT_SAMPLE_RATE_HZ;
static size_t s_frame_samples = BOARD_AUDIO_DEFAULT_FRAME_SAMPLES;
static unsigned s_adc_gain = BOARD_AUDIO_DEFAULT_ADC_GAIN;

static int audio_adapter_configure(void *context, uint32_t sample_rate_hz,
                                   size_t frame_samples)
{
    (void)context;
    if (s_audio_ready || s_record != NULL || s_play != NULL) {
        return BK_ERR_BUSY;
    }
    if ((sample_rate_hz != 8000U && sample_rate_hz != 16000U) ||
        frame_samples == 0U || frame_samples > 960U) {
        return BK_ERR_PARAM;
    }
    s_sample_rate_hz = sample_rate_hz;
    s_frame_samples = frame_samples;
    return BK_OK;
}

/* P8 is PLAY_PWR_CTL; P23 is active-low PLAY_CTL. P8 polarity still needs
 * the artifact-bound voltage check in HARDWARE_ACCEPTANCE.md. */
static bk_err_t amplifier_gpio(gpio_id_t pin, bool high)
{
    bk_err_t rc = gpio_dev_unmap(pin);
    if (rc == BK_OK) rc = bk_gpio_set_output_value(pin, high);
    return rc;
}

bool xiaotai_board_amplifier_prepared(void)
{
    return s_amplifier_prepared;
}

bk_err_t xiaotai_board_amplifier_idle(void)
{
    bk_err_t rc = amplifier_gpio(AMPLIFIER_PLAY_GPIO, true);
    if (rc == BK_OK) rc = amplifier_gpio(AMPLIFIER_POWER_GPIO, false);
    s_amplifier_prepared = false;
    if (rc != BK_OK) {
        BK_LOGE(TAG, "amplifier idle GPIO8=0 GPIO23=1 failed rc=%d\n", rc);
    }
    return rc;
}

bk_err_t xiaotai_board_amplifier_prepare(void)
{
    bk_err_t rc = xiaotai_board_amplifier_idle();
    if (rc == BK_OK) rc = amplifier_gpio(AMPLIFIER_POWER_GPIO, true);
    if (rc != BK_OK) {
        (void)xiaotai_board_amplifier_idle();
        BK_LOGE(TAG, "amplifier prepare failed rc=%d\n", rc);
        return rc;
    }
    rtos_delay_milliseconds(AMPLIFIER_POWER_SETTLE_MS);
    s_amplifier_prepared = true;
    BK_LOGI(TAG, "amplifier power GPIO8=1 play GPIO23=1 (muted)\n");
    return BK_OK;
}

bk_err_t xiaotai_board_amplifier_set_playing(bool playing)
{
    if (!s_amplifier_prepared) return playing ? BK_ERR_NOT_INIT : BK_OK;
    bk_err_t rc = amplifier_gpio(AMPLIFIER_PLAY_GPIO, !playing);
    if (rc != BK_OK) {
        BK_LOGE(TAG, "amplifier play GPIO23 playing=%d failed rc=%d\n",
                playing, rc);
        return rc;
    }
    rtos_delay_milliseconds(AMPLIFIER_MUTE_SETTLE_MS);
    BK_LOGI(TAG, "amplifier play GPIO23=%d playing=%d\n", !playing, playing);
    return BK_OK;
}

void xiaotai_board_amplifier_power_off(void)
{
    if (!s_amplifier_prepared) return;
    bk_err_t rc = amplifier_gpio(AMPLIFIER_POWER_GPIO, false);
    if (rc != BK_OK) {
        BK_LOGE(TAG, "amplifier power GPIO8=0 failed rc=%d\n", rc);
    } else {
        BK_LOGI(TAG, "amplifier power GPIO8=0\n");
    }
    s_amplifier_prepared = false;
}

static void destroy_full_duplex(void)
{
    if (s_record != NULL) {
        audio_record_destroy(s_record);
        s_record = NULL;
    }
    if (s_play != NULL) {
        audio_play_close(s_play);
        audio_play_destroy(s_play);
        s_play = NULL;
    }
    xiaotai_board_amplifier_power_off();
    s_audio_ready = false;
}

static int audio_adapter_start(void *context)
{
    (void)context;
    if (s_audio_ready) return BK_OK;
    if (s_prompt_play != NULL) return BK_ERR_BUSY;

    bk_err_t rc = xiaotai_board_amplifier_prepare();
    if (rc != BK_OK) return rc;

    audio_play_cfg_t play_config = DEFAULT_AUDIO_PLAY_CONFIG();
    play_config.sampRate = s_sample_rate_hz;
    play_config.frame_size = (uint32_t)s_frame_samples;
    play_config.pool_size = (uint32_t)(s_frame_samples * sizeof(int16_t) * 2U);
    s_play = audio_play_create(AUDIO_PLAY_ONBOARD_SPEAKER, &play_config);
    if (s_play == NULL || audio_play_open(s_play) != BK_OK) {
        rc = BK_FAIL;
        goto fail;
    }

    audio_record_cfg_t record_config = DEFAULT_AUDIO_RECORD_CONFIG();
    record_config.sampRate = s_sample_rate_hz;
    record_config.frame_size = (uint32_t)s_frame_samples;
    record_config.pool_size = (uint32_t)(s_frame_samples * sizeof(int16_t) * 2U);
    record_config.adc_gain = (int)s_adc_gain;
    s_record = audio_record_create(AUDIO_RECORD_ONBOARD_MIC, &record_config);
    if (s_record == NULL || audio_record_open(s_record) != BK_OK) {
        rc = BK_FAIL;
        goto fail;
    }

    rc = xiaotai_board_amplifier_set_playing(true);
    if (rc != BK_OK) goto fail;
    s_audio_ready = true;
    return BK_OK;

fail:
    destroy_full_duplex();
    return rc;
}

static int audio_adapter_request_stop(void *context)
{
    (void)context;
    (void)xiaotai_board_amplifier_set_playing(false);
    return s_record == NULL ? BK_OK : audio_record_close(s_record);
}

static int audio_adapter_stop(void *context)
{
    (void)context;
    if (s_record != NULL) (void)audio_record_close(s_record);
    destroy_full_duplex();
    return BK_OK;
}

static bool audio_adapter_ready(void *context)
{
    (void)context;
    return s_audio_ready;
}

static xiaotai_board_audio_format_t audio_adapter_format(void *context)
{
    (void)context;
    return (xiaotai_board_audio_format_t) {
        .sample_rate_hz = s_sample_rate_hz,
        .capture_channels = 1U,
        .playback_channels = 1U,
        .capture_layout = XIAOTAI_CAPTURE_MONO_MIC,
    };
}

static int audio_adapter_read(void *context, int16_t *samples, size_t frames,
                              size_t *bytes_read)
{
    (void)context;
    if (!s_audio_ready || s_record == NULL) return BK_ERR_NOT_INIT;
    if (samples == NULL || bytes_read == NULL || frames == 0U) {
        return BK_ERR_PARAM;
    }
    int rc = audio_record_read_data(s_record, (char *)samples,
                                    (uint32_t)(frames * sizeof(*samples)));
    if (rc < 0) return rc;
    *bytes_read = (size_t)rc;
    return BK_OK;
}

static int audio_adapter_write(void *context, const int16_t *samples,
                               size_t frames, size_t *bytes_written)
{
    (void)context;
    if (!s_audio_ready || s_play == NULL) return BK_ERR_NOT_INIT;
    if (samples == NULL || bytes_written == NULL || frames == 0U) {
        return BK_ERR_PARAM;
    }
    int rc = audio_play_write_data(s_play, (char *)samples,
                                   (uint32_t)(frames * sizeof(*samples)));
    if (rc < 0) return rc;
    *bytes_written = (size_t)rc;
    return BK_OK;
}

static int audio_adapter_set_volume(void *context, unsigned percent)
{
    (void)context;
    (void)percent;
    /* Product mixing currently scales PCM before it reaches the BSP. */
    return BK_OK;
}

static int audio_adapter_set_capture_gain(void *context, unsigned gain)
{
    (void)context;
    if (gain > BOARD_AUDIO_MAX_ADC_GAIN) return BK_ERR_PARAM;
    if (s_record == NULL) {
        s_adc_gain = gain;
        return BK_OK;
    }
    bk_err_t rc = audio_play_set_adc_gain(s_record, (int)gain);
    if (rc == BK_OK) {
        s_adc_gain = gain;
        BK_LOGI(TAG, "microphone ADC gain=0x%02x\n", gain);
    }
    return rc;
}

static int audio_adapter_set_muted(void *context, bool muted)
{
    (void)context;
    if (!s_audio_ready) return muted ? xiaotai_board_amplifier_idle() : BK_OK;
    return xiaotai_board_amplifier_set_playing(!muted);
}

const xiaotai_board_audio_adapter_t *xiaotai_board_audio_adapter(void)
{
    static const xiaotai_board_audio_adapter_t adapter = {
        .configure = audio_adapter_configure,
        .start = audio_adapter_start,
        .request_stop = audio_adapter_request_stop,
        .stop = audio_adapter_stop,
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

bk_err_t xiaotai_board_prompt_audio_start(void)
{
    if (s_audio_ready) return BK_ERR_BUSY;
    if (s_prompt_play != NULL) return BK_OK;
    bk_err_t rc = xiaotai_board_amplifier_prepare();
    if (rc != BK_OK) return rc;

    audio_play_cfg_t config = DEFAULT_AUDIO_PLAY_CONFIG();
    config.sampRate = BOARD_AUDIO_DEFAULT_SAMPLE_RATE_HZ;
    config.frame_size = BOARD_AUDIO_DEFAULT_FRAME_SAMPLES;
    config.pool_size = BOARD_AUDIO_DEFAULT_FRAME_SAMPLES * sizeof(int16_t) * 2U;
    s_prompt_play = audio_play_create(AUDIO_PLAY_ONBOARD_SPEAKER, &config);
    if (s_prompt_play == NULL || audio_play_open(s_prompt_play) != BK_OK ||
        xiaotai_board_amplifier_set_playing(true) != BK_OK) {
        xiaotai_board_prompt_audio_stop();
        return BK_FAIL;
    }
    return BK_OK;
}

int xiaotai_board_prompt_audio_write(const int16_t *samples, size_t frames)
{
    if (s_prompt_play == NULL) return BK_ERR_NOT_INIT;
    if (samples == NULL || frames == 0U) return BK_ERR_PARAM;
    return audio_play_write_data(s_prompt_play, (char *)samples,
                                 (uint32_t)(frames * sizeof(*samples)));
}

void xiaotai_board_prompt_audio_stop(void)
{
    (void)xiaotai_board_amplifier_set_playing(false);
    if (s_prompt_play != NULL) {
        audio_play_close(s_prompt_play);
        audio_play_destroy(s_prompt_play);
        s_prompt_play = NULL;
    }
    xiaotai_board_amplifier_power_off();
}
