"""Production LVGL adapter replay: control identity and displayed room data."""
from pathlib import Path
import unittest
from test_s3_product_regressions import function, run_c

ROOT = Path(__file__).resolve().parents[2]
PRODUCT = ROOT / 'platforms/esp-idf/components/starter_product/src/starter_product.c'


def run_room_case(checks):
    source = PRODUCT.read_text()
    start = source.index('    bool room_changed =')
    end = source.index('\n\n', start)
    try:
        controls = function(source, 'refresh_room_controls')
    except AssertionError:
        controls = 'static void refresh_room_controls(const starter_runtime_product_snapshot_t *room) {(void)room;}'
    code = r'''
#include "/home/workspace/xiaotai/product/include/xiaotai_ui_copy.h"
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
typedef struct node {char text[128];bool visible,disabled;struct node *child;} lv_obj_t;
typedef struct {int code;} lv_event_t;
typedef int lv_event_code_t;
#define LV_EVENT_PRESSED 1
#define LV_EVENT_RELEASED 2
#define LV_EVENT_PRESS_LOST 3
typedef struct {int room_phase;unsigned room_online_count,room_member_count;bool room_password_set,room_ptt;
 char room_code[7],room_message[49];struct {char id[65];bool self,speaking;} room_members[8];} starter_runtime_product_snapshot_t;
static starter_runtime_product_snapshot_t snapshot,product;
static lv_obj_t pool[256];
static unsigned nodes;
static char s_voice_feedback[64];
static uint8_t s_room_members_page;
lv_obj_t *s_room_code_label,*s_room_status_label,*s_room_member_labels[3],*s_room_page_label;
lv_obj_t *s_room_ptt_button,*s_room_connecting_label,*s_room_previous_button,*s_room_next_button,*s_room_members_caption;
#define LV_STATE_DISABLED 1
static void lv_obj_add_state(lv_obj_t *o,int s){if(o)o->disabled=true;}
static void lv_obj_clear_state(lv_obj_t *o,int s){if(o)o->disabled=false;}
#define PAGE_ROOM 1
#define STARTER_ROOM_NONE 0
#define STARTER_ROOM_ERROR 1
#define STARTER_ROOM_JOINED 2
#define STARTER_ROOM_ASSIGNED 3
#define ACTION_MENU 0
#define ACTION_ROOM_CREATE 1
#define ACTION_ROOM_CREATE_PASSWORD 2
#define ACTION_ROOM_JOIN 3
#define ACTION_ROOM 4
#define ACTION_ROOM_LEAVE 5
#define ACTION_ROOM_PREVIOUS 6
#define ACTION_ROOM_NEXT 7
#define LV_EVENT_ALL 0
#define lv_color_hex(x) (x)
static int s_page=PAGE_ROOM,s_previous_room_phase;
static bool s_previous_room_ptt;
static unsigned s_previous_room_online_count;
static char s_previous_room_code[7],s_previous_room_message[49];
static starter_runtime_product_snapshot_t starter_runtime_product_snapshot(void) {return snapshot;}
static void make_header_back_button(lv_obj_t *s,int a) {(void)s;(void)a;}
static void render_header(lv_obj_t *s,const char *t) {(void)s;(void)t;}
static void on_action(lv_event_t *e) {(void)e;}
static lv_event_code_t lv_event_get_code(lv_event_t *e) {return e->code;}
static void note_interaction(void) {}
static int starter_runtime_room_ptt(bool active) {snapshot.room_ptt=active;return 0;}

static lv_obj_t *make_label(lv_obj_t *s,const char *t,int x,int y,int w,unsigned c) {
 (void)s;(void)x;(void)y;(void)w;(void)c;
 assert(nodes<256);lv_obj_t *obj=&pool[nodes++];snprintf(obj->text,128,"%s",t);obj->visible=true;return obj;
}
static lv_obj_t *make_button(lv_obj_t *s,const char *t,int x,int y,int w,int h,int a) {
 (void)h;(void)a;lv_obj_t *obj=make_label(s,"",x,y,w,0);obj->child=make_label(obj,t,0,0,0,0);return obj;
}
static void lv_obj_remove_event_cb(lv_obj_t *s,void (*fn)(lv_event_t *)) {(void)s;(void)fn;}
static void lv_obj_add_event_cb(lv_obj_t *s,void (*fn)(lv_event_t *),int e,void *p) {(void)s;(void)fn;(void)e;(void)p;}
static void label_set_text_if_changed(lv_obj_t *s,const char *t) {if(s) snprintf(s->text,128,"%s",t);}
static void set_button_text(lv_obj_t *s,const char *t) {if(s) label_set_text_if_changed(s->child,t);}
static void set_object_visible(lv_obj_t *s,bool visible) {if(s) s->visible=visible;}
static void sync_room_foreground(void) {}
''' + function(source, 'on_room_ptt') + controls + function(source, 'render_room') + r'''
static void render_page(void) {render_room(NULL);}
static void refresh_tick(void) {
''' + source[start:end] + r'''
 s_previous_room_phase=product.room_phase;s_previous_room_ptt=product.room_ptt;
 s_previous_room_online_count=product.room_online_count;
 strcpy(s_previous_room_code,product.room_code);strcpy(s_previous_room_message,product.room_message);
}
static bool shown(const char *text) {
 for(unsigned i=0;i<nodes;i++) if(pool[i].visible && strstr(pool[i].text,text)) return true;
 return false;
}
int main(void) {
 snapshot.room_phase=STARTER_ROOM_JOINED;snapshot.room_member_count=2;snapshot.room_online_count=100;
 strcpy(snapshot.room_code,"123456");
 strcpy(snapshot.room_members[0].id,"alice");strcpy(snapshot.room_members[1].id,"bob");
 product=snapshot;s_previous_room_phase=STARTER_ROOM_JOINED;s_previous_room_online_count=100;
 render_room(NULL);unsigned initial_nodes=nodes;
''' + checks + '\n}\n'
    run_c(code)


class RoomRefreshRegressions(unittest.TestCase):
    def test_connection_error_keeps_assignment_and_leave_controls(self):
        run_room_case(r'''
 snapshot.room_phase=STARTER_ROOM_ERROR;
 strcpy(snapshot.room_message,"连接失败");product=snapshot;refresh_tick();
 assert(nodes==initial_nodes && shown("123456") && shown("退出房间"));
 assert(!shown("创建房间") && !shown("加入房间"));
 assert(shown("重新进入") && !s_room_ptt_button->visible);
''')

    def test_ptt_feedback_keeps_pressed_widget_and_updates_label(self):
        run_room_case(r'''
 lv_obj_t *pressed=s_room_ptt_button;
 for(unsigned cycle=0;cycle<100;cycle++) {
   lv_event_t event={LV_EVENT_PRESSED};on_room_ptt(&event);
   product=snapshot;refresh_tick();
   assert(snapshot.room_ptt && nodes==initial_nodes && pressed==s_room_ptt_button);
   assert(shown("松开停止"));
   event.code=cycle%2 ? LV_EVENT_RELEASED : LV_EVENT_PRESS_LOST;on_room_ptt(&event);
   product=snapshot;refresh_tick();
   assert(!snapshot.room_ptt && nodes==initial_nodes && pressed==s_room_ptt_button);
   assert(shown("按住说话"));
 }
''')

    def test_member_changes_refresh_without_page_recreation(self):
        run_room_case(r'''
 strcpy(snapshot.room_members[0].id,"carol");snapshot.room_members[0].speaking=true;
 snapshot.room_ptt=true;product=snapshot;refresh_tick();
 assert(shown("carol  正在说话") && !shown("alice"));
 assert(nodes==initial_nodes);
 snapshot.room_members[0].speaking=false;product=snapshot;refresh_tick();
 assert(shown("carol") && !shown("carol  正在说话"));
 snapshot.room_member_count=1;product=snapshot;refresh_tick();assert(!shown("bob"));
 assert(shown("在线 100 人") && nodes==initial_nodes);
''')


if __name__ == '__main__':
    unittest.main()
