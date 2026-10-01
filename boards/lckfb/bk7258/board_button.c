#include "board_button.h"
#include "board_config.h"

#include <driver/gpio.h>

#include "gpio_driver.h"

bk_err_t xiaotai_board_button_init(void)
{
    gpio_dev_unmap(AI_BUTTON);
    bk_err_t rc = bk_gpio_enable_input(AI_BUTTON);
    if (rc == BK_OK) rc = bk_gpio_pull_up(AI_BUTTON);
    return rc;
}

bool xiaotai_board_button_released(void)
{
    return bk_gpio_get_input(AI_BUTTON);
}
