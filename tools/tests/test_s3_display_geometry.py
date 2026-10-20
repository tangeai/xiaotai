"""Run the production LVGL registration against each SPI panel's address space.

ILI9342 is native 320x240; ST7789 is native 240x320. esp_lvgl_port applies
its rotation configuration at registration, overwriting board initialization.
"""
import re
import unittest

from test_s3_product_regressions import ROOT, function, run_c


class S3DisplayGeometryTest(unittest.TestCase):
    def check_registration(self, board, swap, mirror_x, mirror_y):
        source = (ROOT / "platforms/esp-idf/components/starter_product/src/starter_product.c").read_text()
        config = (ROOT / "boards" / board / "board_config.h").read_text()
        # Keep real board constants; only hardware SDK includes are stubbed.
        config = re.sub(r"^#(?:include|pragma).*$", "", config, flags=re.M)
        run_c(r'''
#include <assert.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#define CONFIG_IDF_TARGET_ESP32P4 0
#define ESP_OK 0
#define ESP_ERR_NO_MEM 1
#define ESP_ERR_TIMEOUT 2
#define ESP_RETURN_ON_ERROR(expr, ...) do { int err=(expr); if(err)return err; } while(0)
#define PRODUCT_UI_TASK_PRIORITY 6
#define PRODUCT_UI_TASK_STACK_BYTES 16384
#define PRODUCT_UI_TASK_CORE 0
#define MALLOC_CAP_SPIRAM 1
#define MALLOC_CAP_8BIT 2
#define LV_EVENT_ALL 0
#define pdPASS 1
typedef int esp_err_t;
typedef struct { bool swap_xy, mirror_x, mirror_y; } rotation_t;
typedef struct {
    int task_priority, task_stack, task_affinity, task_max_sleep_ms;
    int task_stack_caps, timer_period_ms;
} lvgl_port_cfg_t;
typedef struct {
    void *io_handle, *panel_handle;
    unsigned buffer_size, trans_size, hres, vres;
    bool double_buffer, monochrome;
    rotation_t rotation;
    struct { bool buff_dma, buff_spiram; } flags;
} lvgl_port_display_cfg_t;
typedef struct { void *disp, *handle; } lvgl_port_touch_cfg_t;
static void *s_display, *s_lcd_io=(void *)1, *s_lcd_panel=(void *)2;
static void *s_touch=(void *)3, *s_call_ring_task;
static unsigned registered, touch_registered;
static int lvgl_port_init(const lvgl_port_cfg_t *p){return ESP_OK;}
static int lvgl_port_lock(int t){return 1;}
static void lvgl_port_unlock(void){}
static void on_screen_event(void){}
static void product_tick(void){}
static void call_ring_task(void *arg){}
static void *lv_scr_act(void){return NULL;}
static void lv_obj_add_event_cb(void *o,void (*cb)(void),int e,void *ctx){}
static void render_page(void){}
static void *lv_timer_create(void (*cb)(void),int ms,void *ctx){return NULL;}
static int xTaskCreate(void (*fn)(void *),const char *name,int stack,void *ctx,
                       int priority,void **task){*task=(void *)4;return pdPASS;}
''' + config + f'''
static void *lvgl_port_add_disp(const lvgl_port_display_cfg_t *cfg) {{
    /* Match the port's registration update: apply config to panel MADCTL. */
    unsigned address_width = cfg->rotation.swap_xy ? LCD_NATIVE_V_RES : LCD_NATIVE_H_RES;
    unsigned address_height = cfg->rotation.swap_xy ? LCD_NATIVE_H_RES : LCD_NATIVE_V_RES;
    assert(cfg->hres == address_width && cfg->vres == address_height);
    assert(cfg->rotation.swap_xy == {int(swap)});
    assert(cfg->rotation.mirror_x == {int(mirror_x)});
    assert(cfg->rotation.mirror_y == {int(mirror_y)});
    assert(cfg->hres == 320 && cfg->vres == 240);
    /* All four LVGL corners must be writable panel addresses. */
    const unsigned xs[] = {{0,319,319,0}}, ys[] = {{0,0,239,239}};
    for(unsigned i=0;i<4;++i) {{
        assert(xs[i]<address_width && ys[i]<address_height);
    }}
    assert(cfg->buffer_size==320*240 && cfg->trans_size==320*LCD_DRAW_LINES);
    registered++;
    return (void *)5;
}}
static void *lvgl_port_add_touch(const lvgl_port_touch_cfg_t *cfg) {{
    assert(registered==1 && cfg->disp==s_display && cfg->handle==s_touch);
    touch_registered++;
    return (void *)6;
}}
''' + function(source, "lvgl_init") + r'''
int main(void) {
    assert(lvgl_init()==ESP_OK);
    assert(registered==1 && touch_registered==1);
}
''')

    def test_cores3_camera_at_top_rotation_survives_lvgl_registration(self):
        self.check_registration("m5stack/cores3-lite", False, True, True)

    def test_lckfb_native_portrait_keeps_existing_landscape_transform(self):
        self.check_registration("lckfb/esp32s3", True, True, False)

    def test_cores3_rotated_touch_corners_match_display_without_overflow(self):
        board = ROOT / "boards/m5stack/cores3-lite"
        config = re.sub(r"^#(?:include|pragma).*$", "",
                        (board / "board_config.h").read_text(), flags=re.M)
        source = (board / "board_display.c").read_text()
        touch_config = re.search(
            r"const esp_lcd_touch_config_t touch = \{.*?\n    \};",
            function(source, "szpi_board_display_init"), re.S).group(0)
        run_c(r'''
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#define GPIO_NUM_NC -1
typedef struct {
    unsigned x_max,y_max;
    int rst_gpio_num,int_gpio_num;
    struct {int reset,interrupt;} levels;
    struct {bool swap_xy,mirror_x,mirror_y;} flags;
} esp_lcd_touch_config_t;
''' + config + r'''
/* esp_lcd_touch 1.2.1's software mirror uses max-coordinate minus raw,
 * rather than resolution minus one minus raw. No second LVGL rotation. */
static void sdk_coordinates(const esp_lcd_touch_config_t *cfg,
                            unsigned *x,unsigned *y){
    if(cfg->flags.mirror_x)*x=cfg->x_max-*x;
    if(cfg->flags.mirror_y)*y=cfg->y_max-*y;
    if(cfg->flags.swap_xy){unsigned tmp=*x;*x=*y;*y=tmp;}
}
int main(void){
''' + touch_config + r'''
    const unsigned raw_x[]={0,319,319,0,160};
    const unsigned raw_y[]={0,0,239,239,120};
    for(unsigned i=0;i<5;++i){
        unsigned x=raw_x[i],y=raw_y[i];
        sdk_coordinates(&touch,&x,&y);
        assert(x==319-raw_x[i] && y==239-raw_y[i]);
        assert(x<LCD_H_RES && y<LCD_V_RES);
    }
}
''')


if __name__ == "__main__":
    unittest.main()
