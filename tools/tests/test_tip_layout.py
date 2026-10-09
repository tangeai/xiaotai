"""Replay the production tip renderer at the LVGL layout boundary."""
import unittest

from test_s3_product_regressions import ROOT, function, run_c


class TipLayout(unittest.TestCase):
    def test_wake_tip_is_centered_on_each_display(self):
        source = (ROOT / 'platforms/esp-idf/components/starter_product/src/starter_product.c').read_text()
        mapping = source[source.index('static lv_coord_t product_x('):source.index('static bool product_uses_centered_layout(')]
        for width, height in ((320, 240), (480, 320), (800, 480)):
            with self.subTest(display=(width, height)):
                run_c(r'''
#include <assert.h>
#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>
typedef int lv_coord_t;
typedef int lv_color_t;
typedef struct obj { struct obj *parent; int x,y,w,align; char text[80]; } lv_obj_t;
typedef struct { int acoustic_score_valid; unsigned probability_milli,threshold_milli; } starter_voice_result_t;
static lv_obj_t top, label;
static lv_obj_t *s_wake_debug_label;
static int64_t s_wake_debug_hide_ms;
static int display_driver_width(void) { return DISPLAY_WIDTH; }
static int display_driver_height(void) { return DISPLAY_HEIGHT; }
static lv_obj_t *lv_layer_top(void) { return &top; }
static lv_obj_t *lv_label_create(lv_obj_t *parent) { label.parent=parent; return &label; }
static void lv_obj_set_pos(lv_obj_t *o,int x,int y) { o->x=x;o->y=y; }
static void lv_obj_set_size(lv_obj_t *o,int w,int h) { (void)h;o->w=w; }
static void lv_obj_set_width(lv_obj_t *o,int w) { o->w=w; }
static void lv_obj_set_height(lv_obj_t *o,int h) { (void)o;(void)h; }
static void lv_obj_set_y(lv_obj_t *o,int y) { o->y=y; }
static void lv_obj_align(lv_obj_t *o,int align,int x,int y) { (void)align;o->x=(o->parent->w-o->w)/2+x;o->y=y; }
static void lv_label_set_text(lv_obj_t *o,const char *text) { snprintf(o->text,sizeof(o->text),"%s",text); }
static void lv_obj_set_style_text_align(lv_obj_t *o,int align,int selector) { (void)selector;o->align=align; }
#define LV_ALIGN_TOP_MID 1
#define LV_TEXT_ALIGN_CENTER 1
#define lv_color_hex(c) (c)
#define lv_label_set_long_mode(...) ((void)0)
#define lv_obj_set_style_text_color(...) ((void)0)
#define lv_obj_set_style_text_font(...) ((void)0)
#define lv_obj_set_style_pad_ver(...) ((void)0)
#define lv_obj_clear_flag(...) ((void)0)
#define set_bg(...) ((void)0)
''' .replace('DISPLAY_WIDTH', str(width)).replace('DISPLAY_HEIGHT', str(height))
                      + (mapping + '\n#define CONFIG_IDF_TARGET_ESP32P4 1\n#define lv_obj_set_pos product_set_pos\n#define lv_obj_set_width product_set_width\n' if width != 320 else '')
                      + function(source, 'make_label')
                      + function(source, 'format_wake_debug')
                      + function(source, 'show_wake_debug') + r'''
int main(void) {
    top.w=display_driver_width();
    show_wake_debug(NULL,100);
    assert(strcmp(label.text,"手动启动")==0);
    assert(label.align==LV_TEXT_ALIGN_CENTER);
    assert(label.x*2+label.w==top.w);
    assert(label.y==36*display_driver_height()/240);
    assert(s_wake_debug_hide_ms==5100);
    starter_voice_result_t result={1,800,500};
    show_wake_debug(&result,200);
    assert(strcmp(label.text,"语音唤醒 0.800 / 阈值 0.500")==0);
    assert(label.x*2+label.w==top.w);
    assert(s_wake_debug_hide_ms==5200);
}
''')


if __name__ == '__main__':
    unittest.main()
