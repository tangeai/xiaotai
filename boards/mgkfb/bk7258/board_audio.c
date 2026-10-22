#include "board_audio.h"
#include "board_config.h"
#include <driver/gpio.h>
#include <os/os.h>
#include "gpio_driver.h"

static bool prepared;
static bk_err_t output(gpio_id_t pin, bool high)
{
    bk_err_t rc = gpio_dev_unmap(pin);
    if (rc == BK_OK) rc = bk_gpio_disable_input(pin);
    if (rc == BK_OK) rc = bk_gpio_set_output_value(pin, high);
    return rc;
}
/* HT6873 CTRL: low shutdown, high enabled. Acoustic gain/timing need HIL.
 * GPIO52 is a shared peripheral rail: audio must never switch it off. */
bk_err_t xiaotai_board_amplifier_idle(void)
{
    prepared = false;
    return output(AMPLIFIER_MUTE_GPIO, false);
}
bk_err_t xiaotai_board_amplifier_prepare(void)
{
    bk_err_t rc = xiaotai_board_amplifier_idle();
    if (rc == BK_OK) rc = output(PERIPHERAL_POWER_GPIO, true);
    if (rc != BK_OK) return rc;
    rtos_delay_milliseconds(20);
    prepared = true;
    return BK_OK;
}
bk_err_t xiaotai_board_amplifier_set_playing(bool playing)
{
    if (!prepared) return playing ? BK_ERR_NOT_INIT : BK_OK;
    bk_err_t rc = output(AMPLIFIER_MUTE_GPIO, playing);
    if (rc == BK_OK) rtos_delay_milliseconds(5);
    return rc;
}
void xiaotai_board_amplifier_power_off(void)
{
    (void)xiaotai_board_amplifier_idle();
}
bool xiaotai_board_amplifier_prepared(void) { return prepared; }
