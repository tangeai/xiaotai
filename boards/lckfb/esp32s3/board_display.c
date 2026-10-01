#include "board_display.h"
#include "board_config.h"
#include "board_bus.h"

#include "driver/spi_master.h"
#include "esp_check.h"
#include "esp_lcd_io_i2c.h"
#include "esp_lcd_panel_vendor.h"
#include "esp_lcd_touch_ft5x06.h"

static const char *TAG = "szpi_display";

esp_err_t szpi_board_display_set_awake(bool awake)
{
    return gpio_set_level(LCD_PIN_BL,
                          awake ? LCD_BACKLIGHT_ON_LEVEL : LCD_BACKLIGHT_OFF_LEVEL);
}

esp_err_t szpi_board_display_init(szpi_board_display_t *display)
{
    if (display == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    *display = (szpi_board_display_t){0};
    const gpio_config_t backlight = {
        .pin_bit_mask = 1ULL << LCD_PIN_BL,
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    ESP_RETURN_ON_ERROR(gpio_config(&backlight), TAG, "backlight GPIO");
    ESP_RETURN_ON_ERROR(szpi_board_display_set_awake(false), TAG, "backlight off");

    const spi_bus_config_t bus = {
        .sclk_io_num = LCD_PIN_SCLK,
        .mosi_io_num = LCD_PIN_MOSI,
        .miso_io_num = GPIO_NUM_NC,
        .quadwp_io_num = GPIO_NUM_NC,
        .quadhd_io_num = GPIO_NUM_NC,
        .max_transfer_sz = LCD_H_RES * LCD_DRAW_LINES * sizeof(uint16_t),
    };
    ESP_RETURN_ON_ERROR(spi_bus_initialize(LCD_HOST, &bus, SPI_DMA_CH_AUTO),
                        TAG, "LCD SPI bus");
    const esp_lcd_panel_io_spi_config_t io = {
        .dc_gpio_num = LCD_PIN_DC,
        .cs_gpio_num = GPIO_NUM_NC,
        .pclk_hz = LCD_PIXEL_CLOCK_HZ,
        .lcd_cmd_bits = 8,
        .lcd_param_bits = 8,
        .spi_mode = 2,
        .trans_queue_depth = 4,
    };
    ESP_RETURN_ON_ERROR(esp_lcd_new_panel_io_spi(
                            (esp_lcd_spi_bus_handle_t)LCD_HOST, &io, &display->io),
                        TAG, "LCD panel IO");
    const esp_lcd_panel_dev_config_t panel = {
        .reset_gpio_num = GPIO_NUM_NC,
        .rgb_ele_order = LCD_RGB_ELEMENT_ORDER_RGB,
        .bits_per_pixel = 16,
    };
    ESP_RETURN_ON_ERROR(esp_lcd_new_panel_st7789(display->io, &panel, &display->panel),
                        TAG, "ST7789 panel");
    ESP_RETURN_ON_ERROR(esp_lcd_panel_reset(display->panel), TAG, "panel reset");
    ESP_RETURN_ON_ERROR(szpi_board_select_lcd(true), TAG, "cannot select LCD");
    ESP_RETURN_ON_ERROR(esp_lcd_panel_init(display->panel), TAG, "panel init");
    ESP_RETURN_ON_ERROR(esp_lcd_panel_invert_color(display->panel, true), TAG,
                        "panel invert");
    ESP_RETURN_ON_ERROR(esp_lcd_panel_swap_xy(display->panel, true), TAG, "panel swap");
    ESP_RETURN_ON_ERROR(esp_lcd_panel_mirror(display->panel, true, false), TAG,
                        "panel mirror");
    ESP_RETURN_ON_ERROR(esp_lcd_panel_disp_on_off(display->panel, true), TAG, "panel on");

    esp_lcd_panel_io_handle_t touch_io = NULL;
    esp_lcd_panel_io_i2c_config_t touch_io_config =
        ESP_LCD_TOUCH_IO_I2C_FT5x06_CONFIG();
    touch_io_config.scl_speed_hz = TOUCH_I2C_HZ;
    ESP_RETURN_ON_ERROR(esp_lcd_new_panel_io_i2c(
                            szpi_board_i2c_bus(), &touch_io_config, &touch_io),
                        TAG, "touch I2C IO");
    const esp_lcd_touch_config_t touch = {
        .x_max = LCD_NATIVE_H_RES,
        .y_max = LCD_NATIVE_V_RES,
        .rst_gpio_num = GPIO_NUM_NC,
        .int_gpio_num = GPIO_NUM_NC,
        .levels = {.reset = 0, .interrupt = 0},
        .flags = {.swap_xy = 1, .mirror_x = 1, .mirror_y = 0},
    };
    ESP_RETURN_ON_ERROR(esp_lcd_touch_new_i2c_ft5x06(
                            touch_io, &touch, &display->touch),
                        TAG, "FT6336 touch");
    return ESP_OK;
}
