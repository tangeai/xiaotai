#include "xiaotai_board_button.h"

#include "board_config.h"
#include "driver/gpio.h"

static int initialize(void *context)
{
    (void)context;
    const gpio_config_t config = {
        .pin_bit_mask = 1ULL << BOARD_AI_BUTTON_GPIO,
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    return gpio_config(&config);
}

static bool read_pressed(void *context)
{
    (void)context;
    return gpio_get_level(BOARD_AI_BUTTON_GPIO) == 0;
}

static const xiaotai_board_button_adapter_t s_adapter = {
    .context = NULL,
    .name = "BOOT GPIO0",
    .initialize = initialize,
    .read_pressed = read_pressed,
};

const xiaotai_board_button_adapter_t *xiaotai_board_button_adapter(void)
{
    return &s_adapter;
}
