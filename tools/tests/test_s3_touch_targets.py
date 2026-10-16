"""Exercise production touch target construction and edge-hit routing."""
import re
import unittest
from test_s3_product_regressions import ROOT, function, run_c


class TouchTargets(unittest.TestCase):
    def test_s3_edges_and_keypad_do_not_overlap(self):
        source = (ROOT / 'platforms/esp-idf/components/starter_product/src/starter_product.c').read_text()
        names = ('make_button', 'make_home_menu_button', 'make_home_wechat_button',
                 'make_header_back_button', 'render_room_input')
        bodies = '\n'.join(function(source, name) for name in names)
        home = function(source, 'render_home')
        tap = home[home.index('    lv_obj_t *tap ='):]
        bodies += '\nstatic void make_home_tap(lv_obj_t *screen) {\n' + tap
        diagnostics = function(source, 'render_diagnostics')
        bodies += diagnostics[:diagnostics.index('    lv_obj_t *panel =')] + '}\n'
        ignored = sorted(set(re.findall(r'\b(lv_obj_set_style_\w+|lv_obj_clear_flag|lv_label_set_text|lv_img_set_src|lv_obj_center)\(', bodies)))
        enums = '\n'.join(re.search(r'typedef enum \{[^}]+\} ' + name + ';', source).group()
                          for name in ('product_page_t', 'product_action_t'))
        run_c(r'''
#include <assert.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include "xiaotai_ui_copy.h"
#define CONFIG_IDF_TARGET_ESP32S3 1
#define lv_color_hex(x) (x)
#define LV_EVENT_CLICKED 1
typedef int lv_coord_t;
typedef struct obj {int x,y,w,h,ext,action; struct obj *parent;} lv_obj_t;
''' + enums + r'''
static lv_obj_t objects[80]; static int used;
static bool s_header_has_back, s_room_input_create;
static product_page_t s_page=PAGE_ROOM_CODE;
static char s_room_input[16],s_voice_feedback[80];
static lv_obj_t *lv_obj_create(lv_obj_t *p){lv_obj_t *o=&objects[used++];*o=(lv_obj_t){.parent=p,.action=-1};return o;}
#define lv_btn_create lv_obj_create
#define lv_label_create lv_obj_create
#define lv_img_create lv_obj_create
static void lv_obj_set_pos(lv_obj_t *o,int x,int y){o->x=x;o->y=y;}
static void lv_obj_set_size(lv_obj_t *o,int w,int h){o->w=w;o->h=h;}
static void lv_obj_set_ext_click_area(lv_obj_t *o,int e){o->ext=e;}
static void on_action(void){}
static void on_home_tap(void){}
static void lv_obj_add_event_cb(lv_obj_t *o,void (*cb)(void),int event,void *data){o->action=cb==on_home_tap?999:(int)(uintptr_t)data;}
static void set_bg(lv_obj_t *o,int color){}
static void render_header(lv_obj_t *o,const char *s){}
static lv_obj_t *make_label(lv_obj_t *p,const char *s,int x,int y,int w,int c){return lv_obj_create(p);}
''' + '\n'.join('#define '+name+'(obj,...) ((void)(obj))' for name in ignored) + '\n' + bodies + r'''
static bool hits(lv_obj_t *o,int x,int y){return o->action>=0 && x>=o->x-o->ext && x<o->x+o->w+o->ext && y>=o->y-o->ext && y<o->y+o->h+o->ext;}
static int click(int x,int y){for(int i=used-1;i>=0;i--)if(hits(&objects[i],x,y))return objects[i].action;return -1;}
static void no_overlap(void){for(int y=0;y<240;y++)for(int x=0;x<320;x++){int n=0;for(int i=0;i<used;i++)n+=hits(&objects[i],x,y);assert(n<=1);}}
int main(void){
 lv_obj_t *menu=make_home_menu_button(NULL),*call=make_home_wechat_button(NULL);
 make_home_tap(NULL);
 assert(menu->w>=48 && menu->h>=48 && call->w>=48 && call->h>=48);
 assert(click(265,180)==ACTION_WECHAT_QUICK);
 assert(click(309,231)==ACTION_MENU);
 assert(click(250,160)==999);
 no_overlap();
 used=0;lv_obj_t *back=make_header_back_button(NULL,ACTION_HOME);
 assert(back->w+2*back->ext>=48 && back->h+2*back->ext>=40);
 assert(click(49,20)==ACTION_HOME);
 /* The first settings row begins at y=40. Expanded back must not cover it. */
 make_button(NULL,"-",8,40,42,38,ACTION_HOME);no_overlap();
 used=0;render_diagnostics(NULL);no_overlap();
 used=0;render_room_input(NULL);
 int keys=0;
 for(int i=0;i<used;i++){
  lv_obj_t *o=&objects[i];if(o->action<0 || o->action==ACTION_ROOM)continue;
  keys++;assert(o->w+2*o->ext>=92 && o->h+2*o->ext>=40);
  assert(o->x-o->ext>=0 && o->x+o->w+o->ext<=320);
  assert(o->y-o->ext>=56 && o->y+o->h+o->ext<=220);
  assert(click(o->x-o->ext,o->y+o->h/2)==o->action);
  assert(click(o->x+o->w+o->ext-1,o->y+o->h/2)==o->action);
 }
 assert(keys==12);no_overlap();
 return 0;
}
''', [ROOT / 'product/include'])


if __name__ == '__main__':
    unittest.main()
