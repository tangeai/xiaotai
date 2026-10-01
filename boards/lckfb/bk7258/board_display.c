#include "board_display.h"
#include "board_config.h"

#include <components/bk_display.h>
#include <driver/gpio.h>
#include <driver/lcd_types.h>

#include "gpio_driver.h"
#include "xiaotai_log.h"

#define TAG "xiaotai_board_display"

static bk_display_ctlr_handle_t s_display;
static bool s_ready;
static bool s_backlight_on;

static const lcd_qspi_init_cmd_t s_st7789_landscape_init[] = {
    {0x00, {0x78}, 0xFF}, {0x11, {0x00}, 0}, {0x00, {0x78}, 0xFF},
    {0x36, {0x60}, 1}, {0x3A, {0x05}, 1},
    {0xB2, {0x3F, 0x3F, 0x00, 0x33, 0x33}, 5},
    {0xB7, {0x35}, 1}, {0xBB, {0x2A}, 1}, {0xC0, {0x2C}, 1},
    {0xC2, {0x01}, 1}, {0xC3, {0x0B}, 1}, {0xC4, {0x20}, 1},
    {0xC6, {0x1F}, 1}, {0xD0, {0xA4, 0xA1}, 2},
    {0xE0, {0xD0, 0x01, 0x08, 0x0F, 0x11, 0x2A, 0x36,
            0x55, 0x44, 0x3A, 0x0B, 0x06, 0x11, 0x20}, 14},
    {0xE1, {0xD0, 0x02, 0x07, 0x0A, 0x0B, 0x18, 0x34,
            0x43, 0x4A, 0x2B, 0x1B, 0x1C, 0x22, 0x1F}, 14},
    {0x21, {0x00}, 0}, {0x35, {0x00}, 0}, {0x29, {0x00}, 0},
};

static const lcd_spi_t s_st7789_landscape_spi = {
    .clk = LCD_QSPI_64M,
    .init_cmd = s_st7789_landscape_init,
    .device_init_cmd_len = sizeof(s_st7789_landscape_init) /
                           sizeof(s_st7789_landscape_init[0]),
    .frame_len = XIAOTAI_BOARD_DISPLAY_WIDTH * XIAOTAI_BOARD_DISPLAY_HEIGHT *
                 CONFIG_LCD_SPI_COLOR_DEPTH_BYTE,
};

static const lcd_device_t s_st7789_landscape = {
    .id = LCD_DEVICE_ST7789V2,
    .name = "st7789v2-landscape",
    .type = LCD_TYPE_SPI,
    .width = XIAOTAI_BOARD_DISPLAY_WIDTH,
    .height = XIAOTAI_BOARD_DISPLAY_HEIGHT,
    .spi = &s_st7789_landscape_spi,
};

static bk_err_t gpio_output(gpio_id_t pin, bool high)
{
    bk_err_t rc = gpio_dev_unmap(pin);
    if (rc == BK_OK) rc = bk_gpio_enable_output(pin);
    if (rc == BK_OK) {
        rc = high ? bk_gpio_set_output_high(pin) :
                    bk_gpio_set_output_low(pin);
    }
    return rc;
}

bk_err_t xiaotai_board_display_init(void)
{
    if (s_ready) return BK_OK;
    bk_err_t rc = gpio_output(LCD_MASTER_POWER, true);
    if (rc == BK_OK) rc = gpio_output(LCD_POWER, true);
    if (rc == BK_OK) rc = gpio_output(LCD_BACKLIGHT, true);
    if (rc != BK_OK) {
        BK_LOGE(TAG, "LCD power GPIO initialization failed rc=%d\n", rc);
        return rc;
    }

    bk_display_spi_ctlr_config_t config = {
        .lcd_device = &s_st7789_landscape,
        .spi_id = XIAOTAI_LCD_SPI_ID,
        .dc_pin = XIAOTAI_LCD_DC_GPIO,
        .reset_pin = XIAOTAI_LCD_RESET_GPIO,
        .te_pin = 0,
    };
    rc = bk_display_spi_new(&s_display, &config);
    if (rc == BK_OK) rc = bk_display_open(s_display);
    if (rc != BK_OK) {
        BK_LOGE(TAG, "ST7789V2 initialization failed rc=%d\n", rc);
        return rc;
    }
    s_ready = true;
    rc = xiaotai_board_display_set_backlight(true);
    if (rc != BK_OK) {
        s_ready = false;
        BK_LOGE(TAG, "LCD backlight enable failed rc=%d\n", rc);
        return rc;
    }
    BK_LOGI(TAG, "ST7789V2 ready 320x240 landscape SPI1\n");
    return BK_OK;
}

bool xiaotai_board_display_ready(void)
{
    return s_ready;
}

bk_err_t xiaotai_board_display_flush(frame_buffer_t *frame,
                                     bk_err_t (*release)(void *arg))
{
    if (!s_ready) return BK_ERR_NOT_INIT;
    return bk_display_flush(s_display, frame, release);
}

bk_err_t xiaotai_board_display_set_backlight(bool enabled)
{
    if (!s_ready) return BK_ERR_NOT_INIT;
    bk_err_t rc = gpio_output(LCD_BACKLIGHT, !enabled);
    if (rc == BK_OK) s_backlight_on = enabled;
    return rc;
}

bool xiaotai_board_display_backlight_on(void)
{
    return s_backlight_on;
}

const void *xiaotai_board_display_probe_lcd(void)
{
    return s_ready ? &s_st7789_landscape : NULL;
}
