#include "board_button.h"
#include "board_config.h"
#include <driver/gpio.h>
#include "gpio_driver.h"
static const gpio_id_t keys[] = {AI_BUTTON, VOLUME_UP_BUTTON, VOLUME_DOWN_BUTTON};
bk_err_t xiaotai_board_button_init(void)
{
    for (unsigned i=0; i<3; ++i) {
        bk_err_t rc = gpio_dev_unmap(keys[i]);
        if (rc == BK_OK) rc = bk_gpio_disable_output(keys[i]);
        if (rc == BK_OK) rc = bk_gpio_enable_input(keys[i]);
        if (rc == BK_OK) rc = bk_gpio_pull_up(keys[i]);
        if (rc != BK_OK) return rc;
    }
    return BK_OK;
}
bool xiaotai_board_key_released(unsigned index)
{
    return index >= 3 || bk_gpio_get_input(keys[index]);
}
bool xiaotai_board_button_released(void) { return xiaotai_board_key_released(0); }
