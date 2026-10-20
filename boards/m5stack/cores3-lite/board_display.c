/* Panel command tables adapted from M5GFX / LovyanGFX (FreeBSD).
 * Copyright and license: boards/m5stack/cores3-lite/THIRD_PARTY.md. */
#include "board_display.h"
#include "board_config.h"
#include "board_power.h"
#include "board_bus.h"
#include "esp_check.h"
#include "esp_lcd_ili9341.h"
#include "esp_lcd_io_i2c.h"
#include "esp_lcd_touch_ft5x06.h"
static const char *TAG = "cores3_display";
esp_err_t szpi_board_display_set_awake(bool awake)
{
    return cores3_board_backlight(awake);
}
#if defined(XIAOTAI_PANEL_ILI9342C)
static const ili9341_lcd_init_cmd_t s_panel_init[] = {
    {0xc8, (const uint8_t[]){0xff,0x93,0x42}, 3, 0},
    {0xc0, (const uint8_t[]){0x12,0x12}, 2, 0},
    {0xc1, (const uint8_t[]){0x03}, 1, 0},
    {0xc5, (const uint8_t[]){0xf2}, 1, 0},
    {0xb0, (const uint8_t[]){0xe0}, 1, 0},
    {0xf6, (const uint8_t[]){0x01,0x00,0x00}, 3, 0},
    {0xe0, (const uint8_t[]){0x00,0x0c,0x11,0x04,0x11,0x08,0x37,0x89,0x4c,0x06,0x0c,0x0a,0x2e,0x34,0x0f}, 15, 0},
    {0xe1, (const uint8_t[]){0x00,0x0b,0x11,0x05,0x13,0x09,0x33,0x67,0x48,0x07,0x0e,0x0b,0x2e,0x33,0x0f}, 15, 0},
    {0xb6, (const uint8_t[]){0x08,0x82,0x1d,0x04}, 4, 0},
    {0x38, NULL, 0, 0}, {0x11, NULL, 0, 120},
};
#else
static const ili9341_lcd_init_cmd_t s_panel_init[] = {
    {0xdd,(const uint8_t[]){1},1,0}, {0xd5,(const uint8_t[]){0},1,0},
    {0xb1,(const uint8_t[]){0x22},1,0}, {0xc8,(const uint8_t[]){0x38},1,0},
    {0xcb,(const uint8_t[]){0x1c},1,0}, {0xc9,(const uint8_t[]){0x1a},1,0},
    {0xca,(const uint8_t[]){0x1a},1,0},
    {0xb7,(const uint8_t[]){0x5a,0x41,0x11,0x19},4,0},
    {0xe4,(const uint8_t[]){4,8,0x11,6,0x12,7,0x3a,0x76,0x47,7,0x0f,0x0a,0x11,0x19,5},15,0},
    {0xe5,(const uint8_t[]){2,3,7,6,0x12,7,0x36,0x5f,0x48,6,0x10,0x0c,0x16,0x14,9},15,0},
    {0x38,NULL,0,0}, {0x11,NULL,0,120},
};
#endif


esp_err_t szpi_board_display_init(szpi_board_display_t *display)
{
    if (!display) return ESP_ERR_INVALID_ARG;
    *display = (szpi_board_display_t){0};
    ESP_RETURN_ON_ERROR(cores3_board_backlight(false), TAG, "backlight off");

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
        .cs_gpio_num = LCD_PIN_CS,
        .pclk_hz = LCD_PIXEL_CLOCK_HZ,
        .lcd_cmd_bits = 8,
        .lcd_param_bits = 8,
        .spi_mode = 0,
        .trans_queue_depth = 4,
    };
    ESP_RETURN_ON_ERROR(esp_lcd_new_panel_io_spi(
                            (esp_lcd_spi_bus_handle_t)LCD_HOST, &io, &display->io),
                        TAG, "LCD panel IO");
    /* Use only the selected ILI9342C/E power and gamma command table. */
    ili9341_vendor_config_t vendor = {
        .init_cmds = s_panel_init, .init_cmds_size = sizeof(s_panel_init)/sizeof(s_panel_init[0]),
    };
    const esp_lcd_panel_dev_config_t panel = {
        .reset_gpio_num = GPIO_NUM_NC,
        .rgb_ele_order = LCD_RGB_ELEMENT_ORDER_BGR,
        .bits_per_pixel = 16,
        .vendor_config = &vendor,
    };
    ESP_RETURN_ON_ERROR(esp_lcd_new_panel_ili9341(display->io, &panel, &display->panel),
                        TAG, "ILI9342 panel transport");
    ESP_RETURN_ON_ERROR(esp_lcd_panel_reset(display->panel), TAG, "panel reset");
    ESP_RETURN_ON_ERROR(szpi_board_select_lcd(true), TAG, "cannot select LCD");
    ESP_RETURN_ON_ERROR(esp_lcd_panel_init(display->panel), TAG, "panel init");
    ESP_RETURN_ON_ERROR(esp_lcd_panel_invert_color(display->panel, true), TAG,
                        "panel invert");
    ESP_RETURN_ON_ERROR(esp_lcd_panel_swap_xy(display->panel, LCD_SWAP_XY), TAG, "panel swap");
    ESP_RETURN_ON_ERROR(esp_lcd_panel_mirror(display->panel, LCD_MIRROR_X, LCD_MIRROR_Y), TAG, "panel mirror");
    ESP_RETURN_ON_ERROR(esp_lcd_panel_disp_on_off(display->panel, true), TAG, "panel on");

    esp_lcd_panel_io_handle_t touch_io = NULL;
    esp_lcd_panel_io_i2c_config_t touch_io_config =
        ESP_LCD_TOUCH_IO_I2C_FT5x06_CONFIG();
    /* Reuse the board's sole driver_ng I2C0 master.  Touch must never create
     * the deprecated panel-IO v1 path because it conflicts before app_main. */
    touch_io_config.scl_speed_hz = TOUCH_I2C_HZ;
    ESP_RETURN_ON_ERROR(esp_lcd_new_panel_io_i2c(
                            szpi_board_i2c_bus(), &touch_io_config, &touch_io),
                        TAG, "touch I2C IO");
    const esp_lcd_touch_config_t touch = {
        /* esp_lcd_touch 1.2.1 mirrors as max - raw: maxima are inclusive. */
        .x_max = LCD_NATIVE_H_RES - 1,
        .y_max = LCD_NATIVE_V_RES - 1,
        .rst_gpio_num = GPIO_NUM_NC,
        .int_gpio_num = GPIO_NUM_NC,
        .levels = {.reset = 0, .interrupt = 0},
        .flags = {.swap_xy = LCD_SWAP_XY, .mirror_x = LCD_MIRROR_X, .mirror_y = LCD_MIRROR_Y},
    };
    ESP_RETURN_ON_ERROR(esp_lcd_touch_new_i2c_ft5x06(touch_io, &touch, &display->touch),
                        TAG, "FT6336 touch");
#if defined(XIAOTAI_PANEL_ILI9342C)
    const char *panel_model = "ILI9342C";
#else
    const char *panel_model = "ILI9342E";
#endif
    ESP_LOGI(TAG, "LCD %s native=%dx%d UI=%dx%d swap_xy=%d mirror=%d/%d",
             panel_model, LCD_NATIVE_H_RES, LCD_NATIVE_V_RES,
             LCD_H_RES, LCD_V_RES, LCD_SWAP_XY, LCD_MIRROR_X, LCD_MIRROR_Y);
    return ESP_OK;
}
