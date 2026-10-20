#include "starter_button.h"

/* Touch-only product: GPIO0 is I2S MCLK; power/RST stay hardware controls.
 * Satisfy the optional controls startup without creating a polling task. */
esp_err_t starter_button_start(void)
{
    return ESP_OK;
}
