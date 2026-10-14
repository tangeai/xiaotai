#include "board_display.h"
#include "board_config.h"

#include <components/bk_display.h>
#include <driver/gpio.h>
#include <driver/lcd_spi.h>
#include <driver/lcd_types.h>
#include <os/os.h>
#include <stdatomic.h>
#include <string.h>

#include "gpio_driver.h"
#include "xiaotai_log.h"

#define TAG "xiaotai_board_display"
#define DISPLAY_QUEUE_DEPTH 8U
#define DISPLAY_TASK_STACK 4096U

typedef struct {
    frame_buffer_t *frame;
    bk_err_t (*release)(void *arg);
    uint32_t generation;
    uint16_t x, y, width, height;
    bool partial;
} display_message_t;

static bk_display_ctlr_handle_t s_display;
static atomic_bool s_ready;
static bool s_backlight_on;
static beken_mutex_t s_display_mutex;
static beken_queue_t s_display_queue;
static beken_thread_t s_display_thread;
static atomic_uint s_display_generation;

static void display_process_message(const display_message_t *message)
{
    if (message == NULL || message->frame == NULL) return;
    if (!atomic_load_explicit(&s_ready, memory_order_acquire) ||
        (message->partial && message->generation !=
            atomic_load_explicit(&s_display_generation, memory_order_acquire))) {
        (void)message->release(message->frame);
        return;
    }
    lcd_display_area_t area = {
        .x_start = message->x,
        .y_start = message->y,
        .x_end = (uint16_t)(message->x + message->width - 1U),
        .y_end = (uint16_t)(message->y + message->height - 1U),
    };
    bk_err_t rc = bk_lcd_spi_partial_display(XIAOTAI_LCD_SPI_ID, &area,
                                              message->frame->frame);
#if CONFIG_LCD_SPI_REFRESH_WITH_QSPI_MAPPING_MODE
    if (rc == BK_OK) rc = bk_lcd_spi_wait_display_complete(XIAOTAI_LCD_SPI_ID);
#endif
    if (rc == BK_OK) {
        (void)message->release(message->frame);
    } else {
        /* DMA may still own the current buffer. Disable further display
         * writes and retain this frame rather than freeing it under DMA. */
        BK_LOGE(TAG, "LCD write failed rc=%d; display disabled\n", rc);
        atomic_store_explicit(&s_ready, false, memory_order_release);
    }
}

static void display_task(beken_thread_arg_t argument)
{
    (void)argument;
    display_message_t message;
    for (;;) {
        if (rtos_pop_from_queue(&s_display_queue, &message,
                                BEKEN_WAIT_FOREVER) == BK_OK)
            display_process_message(&message);
    }
}

static bk_err_t release_region(void *arg)
{
    frame_buffer_display_free((frame_buffer_t *)arg);
    return BK_OK;
}

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
    if (atomic_load_explicit(&s_ready, memory_order_acquire)) return BK_OK;
    bk_err_t rc = rtos_init_mutex(&s_display_mutex);
    if (rc != BK_OK) return rc;
    rc = gpio_output(LCD_MASTER_POWER, true);
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
    rc = gpio_output(LCD_BACKLIGHT, false);
    if (rc != BK_OK) {
        BK_LOGE(TAG, "LCD backlight enable failed rc=%d\n", rc);
        return rc;
    }
    s_backlight_on = true;
    rc = rtos_init_queue(&s_display_queue, "xiaotai_display",
                         sizeof(display_message_t), DISPLAY_QUEUE_DEPTH);
    if (rc == BK_OK) {
        rc = rtos_create_psram_thread(&s_display_thread, 5,
                                      "xiaotai_display", display_task,
                                      DISPLAY_TASK_STACK, NULL);
        if (rc != BK_OK) rtos_deinit_queue(&s_display_queue);
    }
    if (rc != BK_OK) {
        BK_LOGE(TAG, "LCD display worker creation failed rc=%d\n", rc);
        return rc;
    }
    atomic_store_explicit(&s_ready, true, memory_order_release);
    BK_LOGI(TAG, "ST7789V2 ready 320x240 landscape SPI1\n");
    return BK_OK;
}

bool xiaotai_board_display_ready(void)
{
    return atomic_load_explicit(&s_ready, memory_order_acquire);
}

bk_err_t xiaotai_board_display_flush_tagged(frame_buffer_t *frame,
                                            bk_err_t (*release)(void *arg),
                                            uint32_t *generation_out)
{
    if (!xiaotai_board_display_ready()) return BK_ERR_NOT_INIT;
    if (frame == NULL || release == NULL || frame->frame == NULL ||
        frame->size != XIAOTAI_BOARD_DISPLAY_WIDTH *
                       XIAOTAI_BOARD_DISPLAY_HEIGHT * 2U)
        return BK_ERR_PARAM;
    rtos_lock_mutex(&s_display_mutex);
    uint32_t generation = atomic_load_explicit(&s_display_generation,
                                               memory_order_relaxed) + 1U;
    display_message_t message = {
        .frame = frame, .release = release, .generation = generation,
        .x = 0U, .y = 0U,
        .width = XIAOTAI_BOARD_DISPLAY_WIDTH,
        .height = XIAOTAI_BOARD_DISPLAY_HEIGHT,
    };
    bk_err_t rc = rtos_push_to_queue(&s_display_queue, &message, 0);
    if (rc == BK_OK) {
        atomic_store_explicit(&s_display_generation, generation,
                              memory_order_release);
        if (generation_out != NULL) *generation_out = generation;
    }
    rtos_unlock_mutex(&s_display_mutex);
    return rc;
}

bk_err_t xiaotai_board_display_flush(frame_buffer_t *frame,
                                     bk_err_t (*release)(void *arg))
{
    return xiaotai_board_display_flush_tagged(frame, release, NULL);
}

bk_err_t xiaotai_board_display_flush_region(uint16_t x, uint16_t y,
                                            uint16_t width, uint16_t height,
                                            uint8_t *pixels,
                                            uint32_t expected_generation)
{
    if (!xiaotai_board_display_ready()) return BK_ERR_NOT_INIT;
    if (pixels == NULL || width == 0U || height == 0U ||
        x >= XIAOTAI_BOARD_DISPLAY_WIDTH ||
        y >= XIAOTAI_BOARD_DISPLAY_HEIGHT ||
        width > XIAOTAI_BOARD_DISPLAY_WIDTH - x ||
        height > XIAOTAI_BOARD_DISPLAY_HEIGHT - y)
        return BK_ERR_PARAM;
    frame_buffer_t *frame = frame_buffer_display_malloc(
        (uint32_t)width * height * 2U);
    if (frame == NULL) return BK_ERR_NO_MEM;
    memcpy(frame->frame, pixels, (size_t)width * height * 2U);
    rtos_lock_mutex(&s_display_mutex);
    if (expected_generation != atomic_load_explicit(&s_display_generation,
                                                     memory_order_acquire)) {
        rtos_unlock_mutex(&s_display_mutex);
        frame_buffer_display_free(frame);
        return BK_ERR_BUSY;
    }
    display_message_t message = {
        .frame = frame, .release = release_region,
        .generation = expected_generation,
        .x = x, .y = y, .width = width, .height = height,
        .partial = true,
    };
    bk_err_t rc = rtos_push_to_queue(&s_display_queue, &message, 0);
    rtos_unlock_mutex(&s_display_mutex);
    if (rc != BK_OK) frame_buffer_display_free(frame);
    return rc;
}

uint32_t xiaotai_board_display_generation(void)
{
    return atomic_load_explicit(&s_display_generation, memory_order_acquire);
}

bk_err_t xiaotai_board_display_set_backlight(bool enabled)
{
    if (!xiaotai_board_display_ready()) return BK_ERR_NOT_INIT;
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
    return xiaotai_board_display_ready() ? &s_st7789_landscape : NULL;
}
