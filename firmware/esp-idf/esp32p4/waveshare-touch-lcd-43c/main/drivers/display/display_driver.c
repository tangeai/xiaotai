#include "display_driver.h"

#include "bsp/esp32_p4_wifi6_touch_lcd_4_3.h"
#include "esp_check.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_touch.h"
#include "esp_log.h"
#include "esp_lvgl_port.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "sdkconfig.h"

#include "app_task_affinity.h"
#include "hardware_board.h"

static const char *TAG = "display_driver";
typedef uint32_t display_alias_u32_t __attribute__((may_alias));

/*
 * The ST7701 is a RAM-less MIPI-DSI video panel with a fixed 480x800 portrait
 * raster. The product UI runs landscape 800x480, so LVGL renders with its own
 * software rotation (the same proven mechanism the 3.5 board used): the
 * driver exposes the physical 480x800 frame, lv_disp_set_rotation(ROT_270)
 * rotates rendering AND input coordinates.
 *
 * Presentation: LVGL's rotated flush chunks are byte-swapped into the DPI
 * back buffer (the one not being scanned), and the scan-out flip happens
 * through esp_lcd_panel_draw_bitmap on the last chunk of a refresh. The DPI
 * controller applies the flip at its own frame boundary, so the screen only
 * ever shows complete frames. After VSYNC, the changed rectangle is copied
 * from the new front buffer into the recycled back buffer; this keeps partial
 * LVGL refreshes coherent across alternating buffers.
 */
#define DISPLAY_DRIVER_LANDSCAPE_ROTATION LV_DISP_ROT_270
/* Larger rotated strips reduce LVGL software-rotation setup and flush callback
 * overhead for call video. 96 physical lines cost 90 KiB per RGB565 draw
 * buffer and keep three strips available across the 640-pixel video width. */
#define DISPLAY_DRIVER_DRAW_LINES 96
#define DISPLAY_DRIVER_LVGL_TASK_PRIORITY 6
#define DISPLAY_DRIVER_TOUCH_SCROLL_LIMIT_PX  18
#define DISPLAY_DRIVER_TOUCH_SCROLL_THROW     0
#define DISPLAY_DRIVER_TOUCH_GESTURE_LIMIT_PX 45
#define DISPLAY_DRIVER_FLIP_TIMEOUT_MS        34

static lv_disp_t *s_display;
static lv_indev_t *s_touch_indev;
static bool s_initialized;
static display_driver_orientation_t s_orientation = DISPLAY_DRIVER_ORIENTATION_LANDSCAPE;

static esp_lcd_panel_handle_t s_panel;
static uint8_t *s_fb[2];                 /* DPI frame buffers (physical 480x800) */
static uint8_t s_back_fb_index;          /* buffer currently being composed */
static uint8_t s_composed_fb;            /* back buffer with the latest frame */
static SemaphoreHandle_t s_refresh_done_sem;
static bool s_flip_pending;
static bool s_refresh_active;
static bool s_dirty_valid;
static lv_area_t s_dirty_area;
static esp_lcd_touch_handle_t s_touch;

_Static_assert(LV_COLOR_DEPTH == 16, "display flush requires RGB565 LVGL color depth");

static bool IRAM_ATTR display_dsi_refresh_done_cb(
    esp_lcd_panel_handle_t panel,
    esp_lcd_dpi_panel_event_data_t *edata,
    void *user_ctx)
{
    (void)panel;
    (void)edata;
    BaseType_t task_woken = pdFALSE;
    SemaphoreHandle_t sem = (SemaphoreHandle_t)user_ctx;
    if (sem != NULL) {
        xSemaphoreGiveFromISR(sem, &task_woken);
    }
    return task_woken == pdTRUE;
}

static void display_driver_track_dirty(const lv_area_t *area)
{
    if (!s_dirty_valid) {
        s_dirty_area = *area;
        s_dirty_valid = true;
        return;
    }
    s_dirty_area.x1 = LV_MIN(s_dirty_area.x1, area->x1);
    s_dirty_area.y1 = LV_MIN(s_dirty_area.y1, area->y1);
    s_dirty_area.x2 = LV_MAX(s_dirty_area.x2, area->x2);
    s_dirty_area.y2 = LV_MAX(s_dirty_area.y2, area->y2);
}

static bool display_driver_prepare_back_buffer(void)
{
    if (!s_flip_pending) {
        s_dirty_valid = false;
        return true;
    }

    /* Wait until the last requested buffer is actually scanned. The buffer
     * that was front before that flip is now safe to update as the new back
     * buffer. Bring its changed rectangle forward so partial LVGL refreshes
     * never alternate between different generations of the UI. */
    if (xSemaphoreTake(s_refresh_done_sem,
                       pdMS_TO_TICKS(DISPLAY_DRIVER_FLIP_TIMEOUT_MS * 2)) != pdTRUE) {
        ESP_LOGW(TAG, "DSI frame flip timeout; postponing back-buffer update");
        return false;
    }

    if (s_dirty_valid) {
        const uint32_t bytes_per_line = (uint32_t)BSP_LCD_H_RES * sizeof(lv_color_t);
        const size_t copy_bytes = (size_t)(s_dirty_area.x2 - s_dirty_area.x1 + 1) *
                                  sizeof(lv_color_t);
        const uint8_t *from = s_fb[s_composed_fb] +
                              (uint32_t)s_dirty_area.y1 * bytes_per_line +
                              (uint32_t)s_dirty_area.x1 * sizeof(lv_color_t);
        uint8_t *to = s_fb[s_back_fb_index] +
                      (uint32_t)s_dirty_area.y1 * bytes_per_line +
                      (uint32_t)s_dirty_area.x1 * sizeof(lv_color_t);
        for (int32_t y = s_dirty_area.y1; y <= s_dirty_area.y2; ++y) {
            memcpy(to, from, copy_bytes);
            from += bytes_per_line;
            to += bytes_per_line;
        }
    }

    s_flip_pending = false;
    s_dirty_valid = false;
    return true;
}

static void display_driver_flush_cb(lv_disp_drv_t *drv,
                                    const lv_area_t *area,
                                    lv_color_t *color_map)
{
    if (s_fb[0] == NULL || s_fb[1] == NULL) {
        lv_disp_flush_ready(drv);
        return;
    }

    if (!s_refresh_active) {
        if (!display_driver_prepare_back_buffer()) {
            lv_disp_flush_ready(drv);
            return;
        }
        s_refresh_active = true;
    }

    uint8_t *back = s_fb[s_back_fb_index];
    const uint32_t bytes_per_line = (uint32_t)BSP_LCD_H_RES * sizeof(lv_color_t);
    const uint32_t line_words = (uint32_t)(area->x2 - area->x1 + 1);
    const uint16_t *from = (const uint16_t *)color_map;
    uint16_t *to = (uint16_t *)(back + (uint32_t)area->y1 * bytes_per_line +
                                (uint32_t)area->x1 * sizeof(lv_color_t));
    /* The DSI bridge consumes natural-endian RGB565 words while LVGL stores
     * LV_COLOR_16_SWAP order; swap each word while composing the frame. */
    for (int32_t y = area->y1; y <= area->y2; ++y) {
        uint32_t x = 0;
        /* Video strips are normally 32-bit aligned. Swap two RGB565 pixels
         * per load/store on that hot path, while retaining a safe scalar
         * path for odd UI rectangles. */
        if ((((uintptr_t)from | (uintptr_t)to) & 3U) == 0U) {
            const display_alias_u32_t *from32 = (const display_alias_u32_t *)from;
            display_alias_u32_t *to32 = (display_alias_u32_t *)to;
            for (; x + 1U < line_words; x += 2U) {
                uint32_t pair = from32[x / 2U];
                to32[x / 2U] = ((pair & 0x00ff00ffU) << 8U) |
                                ((pair & 0xff00ff00U) >> 8U);
            }
        }
        for (; x < line_words; ++x) {
            to[x] = __builtin_bswap16(from[x]);
        }
        to += BSP_LCD_H_RES;
        from += line_words;
    }
    display_driver_track_dirty(area);
    s_composed_fb = s_back_fb_index;

    if (!lv_disp_flush_is_last(drv)) {
        lv_disp_flush_ready(drv);
        return;
    }

    /* Last chunk: request the completed back buffer. The next refresh waits
     * for VSYNC before recycling the old front buffer. */
    esp_err_t ret = esp_lcd_panel_draw_bitmap(s_panel,
                                              0,
                                              0,
                                              BSP_LCD_H_RES,
                                              BSP_LCD_V_RES,
                                              s_fb[s_composed_fb]);
    if (ret == ESP_OK) {
        s_back_fb_index = (uint8_t)(s_composed_fb ^ 1U);
        while (xSemaphoreTake(s_refresh_done_sem, 0) == pdTRUE) {
            /* Drop VSYNC tokens from before this flip request. */
        }
        s_flip_pending = true;
    } else {
        static int flip_log_count;
        if (flip_log_count++ < 8) {
            ESP_LOGE(TAG, "DSI frame flip failed: %s", esp_err_to_name(ret));
        }
    }
    s_refresh_active = false;
    lv_disp_flush_ready(drv);
}

static void display_driver_touch_read_cb(lv_indev_drv_t *drv, lv_indev_data_t *data)
{
    (void)drv;
    uint16_t x = 0;
    uint16_t y = 0;
    uint16_t strength = 0;
    uint8_t count = 0;
    if (s_touch != NULL && esp_lcd_touch_read_data(s_touch) == ESP_OK) {
        (void)esp_lcd_touch_get_coordinates(s_touch, &x, &y, &strength, &count, 1);
    }
    data->point.x = x;
    data->point.y = y;
    data->state = count > 0 ? LV_INDEV_STATE_PRESSED : LV_INDEV_STATE_RELEASED;
}

static void display_driver_configure_touch(lv_indev_t *indev)
{
    if (indev == NULL || indev->driver == NULL) {
        return;
    }
    indev->driver->scroll_limit = DISPLAY_DRIVER_TOUCH_SCROLL_LIMIT_PX;
    indev->driver->scroll_throw = DISPLAY_DRIVER_TOUCH_SCROLL_THROW;
    indev->driver->gesture_limit = DISPLAY_DRIVER_TOUCH_GESTURE_LIMIT_PX;
    ESP_LOGI(TAG,
             "touch input ready: scroll_limit=%u scroll_throw=%u gesture_limit=%u",
             (unsigned)indev->driver->scroll_limit,
             (unsigned)indev->driver->scroll_throw,
             (unsigned)indev->driver->gesture_limit);
}

esp_err_t display_driver_init(display_driver_handles_t *handles)
{
    if (s_initialized) {
        if (handles != NULL) {
            handles->display = s_display;
            handles->touch_indev = s_touch_indev;
        }
        return ESP_OK;
    }

    lvgl_port_cfg_t lvgl_cfg = ESP_LVGL_PORT_INIT_CONFIG();
    lvgl_cfg.task_stack = 12 * 1024;
    lvgl_cfg.task_priority = DISPLAY_DRIVER_LVGL_TASK_PRIORITY;
    lvgl_cfg.task_affinity = APP_TASK_CORE_UI;
    lvgl_cfg.task_stack_caps = APP_TASK_STACK_CAPS_INTERNAL;
    ESP_RETURN_ON_ERROR(lvgl_port_init(&lvgl_cfg), TAG, "lvgl port init failed");

    esp_lcd_panel_io_handle_t io = NULL;
    ESP_RETURN_ON_ERROR(bsp_display_new(NULL, &s_panel, &io), TAG, "bsp display init failed");

    ESP_RETURN_ON_ERROR(esp_lcd_dpi_panel_get_frame_buffer(s_panel, 2, (void *)&s_fb[0], (void *)&s_fb[1]),
                        TAG, "get DPI frame buffers failed");
    /* DPI starts by scanning fb0. Compose the first UI frame in fb1, while
     * the backlight is still off, and keep both initial generations equal. */
    const size_t frame_bytes = (size_t)BSP_LCD_H_RES * BSP_LCD_V_RES * sizeof(lv_color_t);
    memset(s_fb[0], 0, frame_bytes);
    memset(s_fb[1], 0, frame_bytes);
    s_composed_fb = 0;
    s_back_fb_index = 1;
    ESP_LOGI(TAG, "DSI frame buffers: fb0=%p fb1=%p", s_fb[0], s_fb[1]);

    s_refresh_done_sem = xSemaphoreCreateBinary();
    ESP_RETURN_ON_FALSE(s_refresh_done_sem != NULL, ESP_ERR_NO_MEM, TAG, "refresh semaphore failed");
    const esp_lcd_dpi_panel_event_callbacks_t panel_cbs = {
        .on_refresh_done = display_dsi_refresh_done_cb,
    };
    ESP_RETURN_ON_ERROR(esp_lcd_dpi_panel_register_event_callbacks(s_panel, &panel_cbs, s_refresh_done_sem),
                        TAG, "register DPI refresh callback failed");

    /* LVGL software rotation: the driver keeps the physical 480x800 frame and
     * LVGL rotates rendering and input with DISPLAY_DRIVER_LANDSCAPE_ROTATION. */
    lv_color_t *buf1 = heap_caps_malloc(
        (size_t)BSP_LCD_H_RES * DISPLAY_DRIVER_DRAW_LINES * sizeof(lv_color_t),
        MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    lv_color_t *buf2 = heap_caps_malloc(
        (size_t)BSP_LCD_H_RES * DISPLAY_DRIVER_DRAW_LINES * sizeof(lv_color_t),
        MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    ESP_RETURN_ON_FALSE(buf1 != NULL && buf2 != NULL, ESP_ERR_NO_MEM, TAG, "LVGL draw buffers failed");

    lv_disp_draw_buf_t *draw_buf = malloc(sizeof(lv_disp_draw_buf_t));
    ESP_RETURN_ON_FALSE(draw_buf != NULL, ESP_ERR_NO_MEM, TAG, "draw buf descriptor failed");
    lv_disp_draw_buf_init(draw_buf, buf1, buf2,
                          (size_t)BSP_LCD_H_RES * DISPLAY_DRIVER_DRAW_LINES);

    static lv_disp_drv_t disp_drv;
    lv_disp_drv_init(&disp_drv);
    disp_drv.hor_res = BSP_LCD_H_RES;
    disp_drv.ver_res = BSP_LCD_V_RES;
    disp_drv.flush_cb = display_driver_flush_cb;
    disp_drv.draw_buf = draw_buf;
    /* The RAM-less DSI panel cannot transpose (no swap_xy), so LVGL must
     * software-rotate: draw_buf_rotate transposes each chunk into physical
     * 480x800 coordinates before the flush callback. */
    disp_drv.sw_rotate = 1;
    s_display = lv_disp_drv_register(&disp_drv);
    ESP_RETURN_ON_FALSE(s_display != NULL, ESP_FAIL, TAG, "lvgl display register failed");
    lv_disp_set_rotation(s_display, DISPLAY_DRIVER_LANDSCAPE_ROTATION);
    s_orientation = DISPLAY_DRIVER_ORIENTATION_LANDSCAPE;

    ESP_RETURN_ON_ERROR(bsp_touch_new(NULL, &s_touch), TAG, "bsp touch init failed");

    /* Report native portrait coordinates; LVGL rotates input points with the
     * display rotation (indev_pointer_proc), so the GT911 needs no mapping. */
    static lv_indev_drv_t indev_drv;
    lv_indev_drv_init(&indev_drv);
    indev_drv.type = LV_INDEV_TYPE_POINTER;
    indev_drv.read_cb = display_driver_touch_read_cb;
    s_touch_indev = lv_indev_drv_register(&indev_drv);
    ESP_RETURN_ON_FALSE(s_touch_indev != NULL, ESP_FAIL, TAG, "lvgl touch register failed");
    display_driver_configure_touch(s_touch_indev);

    ESP_RETURN_ON_ERROR(bsp_display_backlight_on(), TAG, "backlight on failed");

    s_initialized = true;
    if (handles != NULL) {
        handles->display = s_display;
        handles->touch_indev = s_touch_indev;
    }

    ESP_LOGI(TAG,
             "display ready: physical=%dx%d ui=%ux%u rotation=%u "
             "present=back-fb-chunks flip=frame-boundary",
             BSP_LCD_H_RES,
             BSP_LCD_V_RES,
             display_driver_width(),
             display_driver_height(),
             (unsigned)DISPLAY_DRIVER_LANDSCAPE_ROTATION);
    return ESP_OK;
}

bool display_driver_is_initialized(void)
{
    return s_initialized;
}

uint16_t display_driver_width(void)
{
    return HARDWARE_BOARD_LCD_WIDTH;
}

uint16_t display_driver_height(void)
{
    return HARDWARE_BOARD_LCD_HEIGHT;
}

display_driver_orientation_t display_driver_get_orientation(void)
{
    return s_orientation;
}

esp_err_t display_driver_set_orientation(display_driver_orientation_t orientation)
{
    ESP_RETURN_ON_FALSE(s_initialized, ESP_ERR_INVALID_STATE, TAG, "display is not initialized");
    ESP_RETURN_ON_FALSE(orientation == DISPLAY_DRIVER_ORIENTATION_PORTRAIT ||
                            orientation == DISPLAY_DRIVER_ORIENTATION_LANDSCAPE,
                        ESP_ERR_INVALID_ARG,
                        TAG,
                        "invalid display orientation");
    if (orientation == s_orientation) {
        return ESP_OK;
    }
    lv_disp_set_rotation(s_display,
                         orientation == DISPLAY_DRIVER_ORIENTATION_PORTRAIT
                             ? LV_DISP_ROT_NONE
                             : DISPLAY_DRIVER_LANDSCAPE_ROTATION);
    s_orientation = orientation;
    ESP_LOGI(TAG, "display orientation: mode=%s ui=%ux%u",
             orientation == DISPLAY_DRIVER_ORIENTATION_PORTRAIT ? "portrait" : "landscape",
             display_driver_width(),
             display_driver_height());
    return ESP_OK;
}

esp_err_t display_driver_blit_rgb565(uint16_t x,
                                     uint16_t y,
                                     uint16_t width,
                                     uint16_t height,
                                     const uint16_t *pixels,
                                     uint32_t *elapsed_us)
{
    /*
     * The ST7701 is a RAM-less DPI video panel; a linear blit cannot map the
     * logical landscape frame onto the physical portrait scan-out. Call video
     * is presented through the LVGL image path (the fallback in display.c).
     */
    (void)x;
    (void)y;
    (void)width;
    (void)height;
    (void)pixels;
    (void)elapsed_us;
    return ESP_ERR_NOT_SUPPORTED;
}
