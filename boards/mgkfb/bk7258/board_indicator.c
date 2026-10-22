#include "board_indicator.h"
#include <driver/gpio.h>
#include "gpio_driver.h"
/* Spec V1.0 p.12 LED1/LED2 GPIO40/41; p.14 anodes driven through resistors. */
int xiaotai_board_indicator_init(void)
{
    for (gpio_id_t pin=GPIO_40; pin<=GPIO_41; ++pin) {
        int rc=gpio_dev_unmap(pin);
        if (rc==BK_OK) rc=bk_gpio_disable_input(pin);
        if (rc==BK_OK) rc=bk_gpio_set_output_value(pin,false);
        if (rc!=BK_OK) return rc;
    }
    return BK_OK;
}
int xiaotai_board_indicator_set(bool red,bool green)
{
    int rc=bk_gpio_set_output_value(GPIO_40,red);
    if (rc==BK_OK) rc=bk_gpio_set_output_value(GPIO_41,green);
    return rc;
}
