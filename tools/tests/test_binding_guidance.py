"""Replay the BK binding renderer and preserve its readable verification code."""
import unittest
from test_s3_product_regressions import ROOT, function, run_c

UI = ROOT / 'firmware/beken/bk7258/lckfb-bk7258-xiaotai/ap/src/xiaotai_ui.c'

class BindingGuidance(unittest.TestCase):
    def test_binding_steps_and_code_survive_voice_failure(self):
        run_c(r'''
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "''' + str(ROOT / 'product/include/xiaotai_ui_copy.h') + r'''"
#include "''' + str(ROOT / 'product/include/xiaotai_binding_countdown.h') + r'''"
#define LCD_WIDTH 320
#define LCD_HEIGHT 240
#define PIXEL_FMT_RGB565 1
#define BK_OK 0
#define BK_LOGE(...) ((void)0)
typedef struct {int fmt,width,height,size;unsigned char *frame;} frame_buffer_t;
static unsigned char pixels[320*240*2];
static frame_buffer_t buffer={0,320,240,sizeof(pixels),pixels};
static bool s_call_microphone_muted;
static unsigned xiaotai_platform_verification_seconds_left(void){return 190;}
static int url,account,bind,code_drawn,flushed,countdowns;
static bool xiaotai_board_display_ready(void){return true;}
static frame_buffer_t *frame_buffer_display_malloc(unsigned size){return &buffer;}
static unsigned status_accent(const char *s){return 0;}
static void rectangle(frame_buffer_t*f,int x,int y,int w,int h,int c){}
static void rectangle_outline(frame_buffer_t*f,int x,int y,int w,int h,int t,int c){}
static void circle(frame_buffer_t*f,int x,int y,int r,int c){}
static void mixed_text(frame_buffer_t*f,int x,int y,const char*s,int scale,int c){}
static void observe(int y,const char*s){
 assert(y>=0 && y+16<=240);
 if(!strcmp(s,"有效期剩余 190 秒")){assert(y==200);countdowns++;}
 if(strstr(s,"https://xiaotai.chat"))url++;
 if(!strcmp(s,"首次使用先注册，已有账号直接登录"))account++;
 if(!strcmp(s,"选择「添加设备」，输入下方验证码"))bind++;
}
static void centered_cjk_text(frame_buffer_t*f,int y,const char*s,int c){observe(y,s);}
static void centered_mixed_text(frame_buffer_t*f,int y,const char*s,int scale,int c){observe(y,s);}
static void centered_text(frame_buffer_t*f,int y,const char*s,int scale,int c){
 assert(!strcmp(s,"012345") && scale==5 && y+35<=240);code_drawn++;
}
static void centered_mixed_in(frame_buffer_t*f,int x,int y,const char*s,int scale,int c,int w){}
static void status_icon(frame_buffer_t*f,const char*s,int c){}
static const char *status_title(const char*s){return s;}
static const char *status_hint(const char*s){return s;}
static void release_frame(frame_buffer_t*f){}
static int xiaotai_board_display_flush(frame_buffer_t*f,void(*release)(frame_buffer_t*)){flushed++;return 0;}
''' + function(UI.read_text(), 'present') + r'''
int main(void){
 present("BIND","012345");present("BIND VOICE ERR","012345");
 assert(url==2 && account==2 && bind==2 && code_drawn==2 && flushed==2 && countdowns==2);
}
''')

    def test_esp_binding_steps_and_drawn_digits_are_centered(self):
        source = (ROOT / 'platforms/esp-idf/components/starter_product/src/starter_product.c').read_text()
        run_c(r'''
#include <assert.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include "''' + str(ROOT / 'product/include/xiaotai_ui_copy.h') + r'''"
#include "''' + str(ROOT / 'product/include/xiaotai_binding_countdown.h') + r'''"
#include "''' + str(ROOT / 'product/include/xiaotai_verification_code.h') + r'''"
typedef struct {int x1,y1,x2,y2;} lv_area_t;
typedef struct {int bg_color,bg_opa,border_width,radius;} lv_draw_rect_dsc_t;
typedef int lv_draw_ctx_t;
typedef int lv_obj_t;
typedef int lv_event_t;
typedef struct {lv_draw_ctx_t *draw;lv_area_t area;lv_draw_rect_dsc_t style;} binding_code_draw_t;
#define LV_OBJ_FLAG_SCROLLABLE 1
#define LV_OBJ_FLAG_CLICKABLE 2
#define LV_TEXT_ALIGN_CENTER 1
#define LV_EVENT_DRAW_MAIN 3
#define LV_OPA_COVER 255
static int object,steps,aligned,draws,countdowns,successes,scale=1;
static char s_previous_verification_code[]="012345";
static bool provisioning=true;
static bool s_binding_saved;
#define atomic_load(p) (*(p))
static lv_obj_t *s_binding_countdown_label;
static unsigned s_binding_displayed_seconds;
static unsigned platform_client_verification_seconds_left(void){return provisioning ? 190 : 0;}
static int lv_color_hex(int c){return c;}
static void render_header(lv_obj_t*s,const char*t){assert(!strcmp(t,"绑定设备"));}
static lv_obj_t*make_label(lv_obj_t*s,const char*t,int x,int y,int w,int c){
 if(!strcmp(t,XIAOTAI_UI_BIND_SUCCESS)){assert(y==112 && x==8 && w==304);successes++;}
 if(!strcmp(t,"有效期剩余 190 秒") || !strcmp(t,"正在刷新验证码…")){assert(y==191 && x==8 && w==304);countdowns++;}
 if(strstr(t,"https://xiaotai.chat") || !strcmp(t,"首次使用先注册，已有账号直接登录") || !strcmp(t,"选择「添加设备」，输入下方验证码")){
  assert(y>=34 && y+16<=120 && x==8 && w==304);steps++;
 }
 return &object;
}
static lv_obj_t*lv_obj_create(lv_obj_t*s){return &object;}
static void lv_obj_set_pos(lv_obj_t*s,int x,int y){assert(x==20 && y==120);}
static void lv_obj_set_size(lv_obj_t*s,int w,int h){assert(w==280 && h==66);}
static void set_bg(lv_obj_t*s,int c){}
static void lv_obj_set_style_radius(lv_obj_t*s,int r,int p){}
static void lv_obj_clear_flag(lv_obj_t*s,int f){}
static void lv_obj_set_style_text_align(lv_obj_t*s,int a,int p){assert(a==LV_TEXT_ALIGN_CENTER);aligned++;}
static bool platform_client_provisioning(void){return provisioning;}
static lv_draw_ctx_t *lv_event_get_draw_ctx(lv_event_t*e){return &object;}
static lv_obj_t *lv_event_get_target(lv_event_t*e){return &object;}
static void lv_obj_get_coords(lv_obj_t*o,lv_area_t*a){*a=(lv_area_t){80,120,80+280*scale-1,120+66*scale-1};}
static void lv_draw_rect_dsc_init(lv_draw_rect_dsc_t*d){memset(d,0,sizeof(*d));}
static void lv_draw_rect(lv_draw_ctx_t*d,lv_draw_rect_dsc_t*s,lv_area_t*a){
 assert(s->bg_color==0xFFE066 && s->bg_opa==255);
 assert(a->x1>=80 && a->x2<80+280*scale && a->y1>=120 && a->y2<120+66*scale);draws++;
}
static void lv_obj_add_event_cb(lv_obj_t*o,void(*cb)(lv_event_t*),int event,void*u){assert(event==LV_EVENT_DRAW_MAIN);cb(&object);}
''' + function(source, 'binding_code_fill') + function(source, 'binding_code_draw') + function(source, 'render_binding') + r'''
int main(void){render_binding(&object);scale=2;provisioning=false;render_binding(&object);assert(steps==6 && aligned==10 && countdowns==2 && draws>100);s_binding_saved=true;render_binding(&object);assert(successes==1 && aligned==11 && countdowns==2 && steps==6); }
''')

    def test_geometric_digits_keep_leading_zero_and_fit_both_layouts(self):
        run_c(r'''
#include <assert.h>
#include <limits.h>
#include <stdlib.h>
#include "''' + str(ROOT / 'product/include/xiaotai_verification_code.h') + r'''"
static int w,h,minx,miny,maxx,maxy,count;
static void fill(void*c,int x,int y,int width,int height){
 assert(x>=0 && y>=0 && x+width<=w && y+height<=h);
 if(x<minx)minx=x;
 if(y<miny)miny=y;
 if(x+width>maxx)maxx=x+width;
 if(y+height>maxy)maxy=y+height;
 count++;
}
static int render(const char *code,int width,int height){
 w=width;h=height;minx=miny=INT_MAX;maxx=maxy=count=0;
 xiaotai_verification_code_draw(code,w,h,fill,0);
 assert(count>0 && abs(miny-(h-maxy))<=1);
 if(code==0 || code[0]!='1')assert(abs(minx-(w-maxx))<=1);
 return count;
}
int main(void){
 int compact=render("012345",280,66);assert(maxy-miny>=49);
 assert(render("012345",560,132)==compact);assert(maxy-miny>=98);
 assert(render("112345",280,66)!=compact);
 render("",280,66);render(0,280,66);render("12345x",280,66);
 count=0;xiaotai_verification_code_draw("012345",20,5,fill,0);assert(count==0);
}
''')

if __name__ == '__main__':
    unittest.main()
