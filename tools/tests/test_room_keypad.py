"""Click production-rendered room keys through the ESP UI action dispatcher."""
import re
import unittest

from test_s3_product_regressions import ROOT, function, run_c


class RoomKeypad(unittest.TestCase):
    def test_room_entry_groups_creation_and_join_buttons(self):
        source = (ROOT / 'platforms/esp-idf/components/starter_product/src/starter_product.c').read_text()
        enums = '\n'.join(re.search(r'typedef enum \{[^}]+\} ' + name + ';', source).group()
                          for name in ('product_page_t', 'product_action_t'))
        run_c(r'''
#include "/home/workspace/xiaotai/product/include/xiaotai_ui_copy.h"
#include <assert.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
typedef int lv_obj_t;
#define lv_color_hex(c) (c)
#define STARTER_ROOM_NONE 0
#define STARTER_ROOM_ERROR 1
#define LV_EVENT_ALL 0
typedef struct {char room_code[8],room_message[80];int room_phase;} starter_runtime_product_snapshot_t;
''' + enums + r'''
static char s_voice_feedback[80];
static lv_obj_t *s_room_code_label,*s_room_status_label,*s_room_member_labels[3],
 *s_room_page_label,*s_room_ptt_button,*s_room_connecting_label,*s_room_previous_button,*s_room_next_button,*s_room_members_caption;
static struct {const char *label;int x,y,w,h;product_action_t action;} buttons[16];
static unsigned count;
static int scenario;
static starter_runtime_product_snapshot_t starter_runtime_product_snapshot(void){
 starter_runtime_product_snapshot_t s={0};s.room_phase=scenario;return s;
}
static lv_obj_t *make_header_back_button(lv_obj_t *o,product_action_t a){return o;}
static void render_header(lv_obj_t *o,const char *text){}
static lv_obj_t *make_label(lv_obj_t *o,const char *t,int x,int y,int w,int c){return o;}
static lv_obj_t *make_button(lv_obj_t *o,const char *t,int x,int y,int w,int h,product_action_t a){
 assert(count<16);buttons[count].label=t;buttons[count].x=x;buttons[count].y=y;
 buttons[count].w=w;buttons[count].h=h;buttons[count++].action=a;return o;
}
static void on_action(void){}
static void on_room_ptt(void){}
static void lv_obj_remove_event_cb(lv_obj_t *o,void (*f)(void)){}
static void lv_obj_add_event_cb(lv_obj_t *o,void (*f)(void),int e,void *u){}
static void refresh_room_controls(const starter_runtime_product_snapshot_t *s){}
''' + function(source, 'render_room') + r'''
int main(void){
 for(scenario=STARTER_ROOM_NONE;scenario<=STARTER_ROOM_ERROR;scenario++){
  count=0;render_room(NULL);assert(count==3);
  assert(strcmp(buttons[0].label,"无密码创建")==0 && buttons[0].action==ACTION_ROOM_CREATE);
  assert(strcmp(buttons[1].label,"有密码创建")==0 && buttons[1].action==ACTION_ROOM_CREATE_PASSWORD);
  assert(strcmp(buttons[2].label,"加入房间")==0 && buttons[2].action==ACTION_ROOM_JOIN);
  assert(buttons[0].y==buttons[1].y && buttons[0].w==buttons[1].w && buttons[0].h==buttons[1].h);
  assert(buttons[0].x+buttons[0].w<buttons[1].x);
  assert(buttons[2].y>buttons[0].y+buttons[0].h);
  assert(buttons[2].x==buttons[0].x && buttons[2].x+buttons[2].w==buttons[1].x+buttons[1].w);
 }
 return 0;
}
''')

    def test_rendered_keys_preserve_digits_and_room_flow(self):
        source = (ROOT / 'platforms/esp-idf/components/starter_product/src/starter_product.c').read_text()
        enums = '\n'.join(re.search(r'typedef enum \{[^}]+\} ' + name + ';', source).group()
                          for name in ('product_page_t', 'product_action_t'))
        handler = function(source, 'on_action')
        start = handler.index('action == ACTION_ROOM) {')
        end = handler.index('} else if (action >= ACTION_DIAG_SYSTEM', start)
        dispatch = 'static void dispatch(product_action_t action) { if (' + handler[start:end] + '} }'
        run_c(r'''
#include "/home/workspace/xiaotai/product/include/xiaotai_ui_copy.h"
#include <assert.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
typedef int lv_obj_t;
typedef int esp_err_t;
#define lv_color_hex(c) (c)
#define LV_TEXT_ALIGN_CENTER 0
''' + enums + r'''
static product_page_t s_page;
static char s_room_input[16], s_room_join_code[16], s_voice_feedback[80];
static bool s_room_input_create;
static unsigned s_room_members_page;
static int joins, leaves, creates;
static char joined_code[16], joined_password[16], shown_value[16];
static struct { const char *label; product_action_t action; } buttons[16];
static unsigned button_count;
static bool hint_seen;
static int starter_runtime_room_set_foreground(bool value) { return 0; }
static int starter_runtime_room_refresh(void) { return 0; }
static int starter_runtime_room_create(const char *password) { creates++; return 0; }
static int starter_runtime_room_leave(void) { leaves++; return 0; }
static int starter_runtime_room_join(const char *code, const char *password) {
    joins++; strcpy(joined_code,code); strcpy(joined_password,password); return 0;
}
static bool room_action_submitted(int result) { return result==0; }
static lv_obj_t *make_header_back_button(lv_obj_t *o, product_action_t a) { return o; }
static void render_header(lv_obj_t *o, const char *text) {}
static lv_obj_t *make_label(lv_obj_t *o, const char *text, int x,int y,int w,int c) {
    if (y==34) strcpy(shown_value,text);
    if(strcmp(text,"无密码直接点击确定")==0) hint_seen=true;
    return o;
}
static void lv_obj_set_style_text_align(lv_obj_t *o,int a,int b) {}
static lv_obj_t *make_button(lv_obj_t *o,const char *label,int x,int y,int w,int h,product_action_t a) {
    assert(button_count<16); buttons[button_count].label=label;
    buttons[button_count++].action=a; return o;
}
''' + dispatch + function(source, 'render_room_input') + r'''
static void click(const char *label) {
    button_count=0; render_room_input(NULL);
    for(unsigned i=0;i<button_count;i++) if(strcmp(buttons[i].label,label)==0) {
        dispatch(buttons[i].action); return;
    }
    assert(!"missing rendered button");
}
int main(void) {
    for(unsigned digit=0;digit<10;digit++) {
        dispatch(ACTION_ROOM_JOIN);
        char key[2]={(char)('0'+digit),0}; click(key);
        assert(s_page==PAGE_ROOM_CODE);
        assert(strcmp(s_room_input,key)==0);
        button_count=0; render_room_input(NULL); assert(strcmp(shown_value,key)==0);
        click("删除"); assert(s_room_input[0]==0);
        click("删除"); assert(s_room_input[0]==0);
    }
    dispatch(ACTION_ROOM_JOIN); click("确定"); assert(s_page==PAGE_ROOM_CODE);
    click("1"); click("9"); click("0"); click("2"); click("3"); click("4"); click("5");
    assert(strcmp(s_room_input,"190234")==0); assert(joins==0 && leaves==0 && creates==0);
    click("确定"); assert(s_page==PAGE_ROOM_PASSWORD && s_room_input[0]==0);
    click("9"); button_count=0; render_room_input(NULL); assert(strcmp(shown_value,"*")==0);
    click("删除"); click("0"); click("1"); click("2"); click("3"); click("4");
    assert(strcmp(s_room_input,"0123")==0); click("确定");
    assert(joins==1 && strcmp(joined_code,"190234")==0 && strcmp(joined_password,"0123")==0);
    assert(s_page==PAGE_ROOM);
    dispatch(ACTION_ROOM_JOIN);
    for(int i=0;i<6;i++) click("8");
    click("确定");
    button_count=0;render_room_input(NULL);assert(button_count==12);
    click("1");click("确定");assert(joins==1 && s_page==PAGE_ROOM_PASSWORD);
    click("删除");click("确定");
    assert(joins==2 && strcmp(joined_code,"888888")==0 && joined_password[0]==0 && hint_seen);
    dispatch(ACTION_ROOM_CREATE_PASSWORD); click("1"); click("2"); click("3"); click("4");
    click("确定"); assert(creates==1 && s_page==PAGE_ROOM);
    dispatch(ACTION_ROOM_LEAVE); assert(s_page==PAGE_ROOM_LEAVE_CONFIRM && leaves==0);
    dispatch(ACTION_ROOM_LEAVE_CANCEL); assert(s_page==PAGE_ROOM && leaves==0);
    dispatch(ACTION_ROOM_LEAVE); dispatch(ACTION_ROOM_LEAVE_CONFIRM); assert(leaves==1);
}
''')


if __name__ == '__main__':
    unittest.main()
