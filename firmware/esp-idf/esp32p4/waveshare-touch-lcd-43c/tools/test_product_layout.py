#!/usr/bin/env python3
"""Test actual logical-to-physical LVGL boundary functions without a board."""
from pathlib import Path
import subprocess
import tempfile

root = Path(__file__).resolve().parents[1]
source = (Path(__file__).resolve().parents[5] / "platforms/esp-idf/components/starter_product/src/starter_product.c").read_text()
start = source.index("static lv_coord_t product_x(")
end = source.index("static bool product_uses_centered_layout", start)
body = r'''
#include <assert.h>
#include <stdint.h>
typedef int16_t lv_coord_t;
typedef struct {int x,y,w,h;} lv_obj_t;
#define LCD_H_RES 480
#define LCD_V_RES 320
static int display_width=800, display_height=480;
static int display_driver_width(void) { return display_width; }
static int display_driver_height(void) { return display_height; }
static void lv_obj_set_pos(lv_obj_t *o,int x,int y) {o->x=x;o->y=y;}
static void lv_obj_set_size(lv_obj_t *o,int w,int h) {o->w=w;o->h=h;}
static void lv_obj_set_width(lv_obj_t *o,int w) {o->w=w;}
static void lv_obj_set_height(lv_obj_t *o,int h) {o->h=h;}
'''
body += source[start:end]
body += r'''
int main(void) {
    lv_obj_t o={0};
    assert(product_x(320)==640 && product_y(240)==480);
    assert(product_x(160)==320 && product_y(120)==240);
    product_set_pos(&o,110,194); product_set_size(&o,100,36);
    assert(o.x+o.w/2==320 && o.y+o.h<=480); /* centered in 640px canvas */
    product_set_pos(&o,10,40); product_set_size(&o,300,188);
    assert(o.x==20 && o.w==600 && o.y+o.h<=480); /* AI history */
    product_set_pos(&o,170,125); product_set_size(&o,132,48);
    assert(o.x==340 && o.x+o.w==604 && o.y+o.h<=480); /* video button */
    product_set_width(&o,304); product_set_height(&o,45);
    assert(o.w==608 && o.h==90);
    int zoom=256*480/240;
    int visible=220*zoom/256;
    int face_left=product_x(50)+(product_x(220)-visible)/2;
    assert(face_left+visible/2==320);
    display_width=480; display_height=320;
    assert(product_x(320)==480 && product_y(240)==320); /* 3.5-inch baseline */
    return 0;
}
'''
with tempfile.TemporaryDirectory(prefix="p4-layout-") as directory:
    test = Path(directory)
    (test / "test.c").write_text(body)
    subprocess.run(["cc", "-std=c11", "-Wall", "-Wextra", "-Werror",
                    str(test / "test.c"), "-o", str(test / "test")], check=True)
    subprocess.run([str(test / "test")], check=True)
assert '"视频通话", 330' not in source
assert "lv_obj_set_pos(s_wifi_signal, LCD_H_RES" not in source
assert "product_create_content" in source
assert "ui_font_cn_24" in source
wechat = source[source.index("static lv_obj_t *make_home_wechat_button"):
                source.index("static lv_obj_t *make_header_back_button")]
assert "lv_img_set_src(icon, &s_wechat_call_icon)" in wechat
assert "lv_obj_set_style_img_recolor" in wechat
assert "lv_img_set_zoom(icon" not in wechat
print("PASS: Waveshare 4.3-inch centered 4:3 canvas with uniform geometry and font scale")
