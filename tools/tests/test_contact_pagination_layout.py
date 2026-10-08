"""Exercise the production contact page at the LVGL object/layout boundary."""
import unittest
from test_s3_product_regressions import ROOT, function, run_c

class ContactPaginationLayout(unittest.TestCase):
    def test_manual_pages_keep_all_rows_clear_of_footer(self):
        source=(ROOT/'platforms/esp-idf/components/starter_product/src/starter_product.c').read_text()
        render=function(source,'render_contacts')
        run_c(r'''
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
typedef int lv_coord_t;
typedef int product_action_t;
typedef struct obj {struct obj *parent;int x,y,w,h,pad,border,action,disabled;} lv_obj_t;
static lv_obj_t objects[128];static unsigned used;
static lv_obj_t *lv_obj_create(lv_obj_t *parent){
    lv_obj_t *o=&objects[used++];*o=(lv_obj_t){.parent=parent,.pad=16,.border=2};return o;
}
static void lv_obj_set_pos(lv_obj_t *o,int x,int y){o->x=x;o->y=y;}
static void lv_obj_set_size(lv_obj_t *o,int w,int h){o->w=w;o->h=h;}
static void lv_obj_set_style_pad_all(lv_obj_t *o,int n,int sel){(void)sel;o->pad=n;}
static void lv_obj_set_style_border_width(lv_obj_t *o,int n,int sel){(void)sel;o->border=n;}
static lv_obj_t *make_button(lv_obj_t *p,const char *s,int x,int y,int w,int h,int a){
    (void)s;lv_obj_t *o=lv_obj_create(p);lv_obj_set_pos(o,x,y);lv_obj_set_size(o,w,h);o->action=a;return o;
}
static lv_obj_t *make_label(lv_obj_t *p,const char *s,int x,int y,int w,int color){
    (void)s;(void)color;lv_obj_t *o=lv_obj_create(p);lv_obj_set_pos(o,x,y);o->w=w;return o;
}
static void lv_obj_add_state(lv_obj_t *o,int s){(void)s;o->disabled=1;}
#define lv_label_create lv_obj_create
#define lv_color_hex(n) (n)
#define set_bg(o,...) ((void)(o))
#define lv_obj_set_style_radius(o,...) ((void)(o))
#define lv_obj_clear_flag(o,...) ((void)(o))
#define lv_obj_set_scrollbar_mode(o,...) ((void)(o))
#define lv_label_set_text(o,...) ((void)(o))
#define lv_obj_set_style_text_font(o,...) ((void)(o))
#define lv_obj_set_style_text_color(o,...) ((void)(o))
#define lv_obj_set_width(o,...) ((void)(o))
#define lv_obj_set_height(o,...) ((void)(o))
#define lv_label_set_long_mode(o,...) ((void)(o))
#define lv_obj_center(o,...) ((void)(o))
#define lv_obj_set_style_text_align(o,...) ((void)(o))
#define make_header_back_button(...) ((void)0)
#define render_header(...) ((void)0)
#define CONTACTS_PER_PAGE 3U
#define STARTER_CONTACT_WECHAT 1
#define ACTION_CONTACT_BASE 100
#define ACTION_CONTACT_CALL_BASE 200
#define ACTION_CONTACTS_PREVIOUS 10
#define ACTION_CONTACTS_NEXT 11
#define LV_STATE_DISABLED 1
#define ACTION_MENU 0
#define LV_OBJ_FLAG_SCROLLABLE 1
#define LV_OBJ_FLAG_CLICKABLE 2
#define LV_SCROLLBAR_MODE_OFF 0
#define LV_RADIUS_CIRCLE 255
#define LV_LABEL_LONG_DOT 0
#define LV_TEXT_ALIGN_CENTER 0
typedef struct {int source,online;char name[20];} starter_product_contact_t;
typedef struct {uint8_t contact_count;starter_product_contact_t contacts[8];} starter_runtime_product_snapshot_t;
static starter_runtime_product_snapshot_t snapshot;
static starter_runtime_product_snapshot_t starter_runtime_product_snapshot(void){return snapshot;}
static uint8_t s_contacts_page;
''' + render + r'''
static void check_page(unsigned count,unsigned requested){
    used=0;snapshot.contact_count=count;s_contacts_page=requested;
    for(unsigned i=0;i<count;i++){snapshot.contacts[i].source=i%2;snapshot.contacts[i].online=1;}
    lv_obj_t screen={.w=320,.h=240};render_contacts(&screen);
    unsigned pages=count?(count+2)/3:1,expected=requested<pages?requested:pages-1;
    assert(s_contacts_page==expected);
    unsigned rows=0,calls=0;lv_obj_t *prev=NULL,*next=NULL;
    for(unsigned n=0;n<used;n++){
        lv_obj_t *o=&objects[n];
        if(o->action>=100 && o->action<108){
            assert(o->parent==&screen); /* no padded/clipping list card */
            assert(o->pad==0 && o->border==0);
            assert(o->x>=12 && o->x+o->w<=308);
            assert(o->y>=36 && o->y+o->h<=170); /* footer starts at180 */
            assert(o->action==100+(int)(expected*3+rows));rows++;
        }
        if(o->action>=200 && o->action<208){
            lv_obj_t *p=o->parent;int inset=p->pad+p->border;
            assert(o->x>=0 && o->y>=0);
            assert(o->x+o->w<=p->w-2*inset && o->y+o->h<=p->h-2*inset);
            assert(o->action==p->action+100);calls++;
        }
        if(o->action==10)prev=o;
        if(o->action==11)next=o;
    }
    unsigned remaining=count-expected*3;
    assert(rows==(remaining<3?remaining:3) && calls==rows);
    assert(prev && next && prev->y>=180 && next->y>=180);
    assert(prev->disabled==(expected==0));assert(next->disabled==(expected+1>=pages));
}
int main(void){check_page(0,0);check_page(1,0);check_page(3,0);check_page(5,0);check_page(5,1);check_page(5,7);}
''')

if __name__=='__main__':unittest.main()
