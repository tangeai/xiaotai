#include "board_audio.h"
#include "board_config.h"
#include <driver/gpio.h>
#include <os/os.h>
#include "gpio_driver.h"
#include "xiaotai_log.h"
#define TAG "xiaotai_board_audio"
#define AMPLIFIER_POWER_SETTLE_MS 20U
#define AMPLIFIER_MUTE_SETTLE_MS 5U
static bool s_amplifier_prepared;

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
