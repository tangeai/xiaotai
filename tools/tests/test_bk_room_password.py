"""Drive production BK touch routing and hit boxes to inspect room intents."""
import re
import unittest
from test_s3_product_regressions import ROOT, function, run_c

BASE = ROOT / 'firmware/beken/bk7258/lckfb-bk7258-xiaotai/ap'

class BKRoomPassword(unittest.TestCase):
    def test_password_hint_and_three_column_keys(self):
        source=(BASE / 'src/xiaotai_ui.c').read_text()
        run_c(r'''
#include <assert.h>
#include <stdbool.h>
#include <stddef.h>
#include <string.h>
#include "''' + str(ROOT / 'product/include/xiaotai_ui_copy.h') + r'''"
typedef int frame_buffer_t;
static frame_buffer_t frame;
static unsigned keys;
static bool hint,confirm,skip;
static void xiaotai_metrics_set_state(const char *state){}
static frame_buffer_t *page_frame(const char *title){return &frame;}
static void centered_mixed_in(frame_buffer_t *f,int x,int y,const char *value,int scale,int color,int width){
 if(strcmp(value,"无密码直接点击确定")==0)hint=true;
 if(strcmp(value,"确定")==0)confirm=true;
 if(strcmp(value,"无密码")==0)skip=true;
}
static void rectangle(frame_buffer_t *f,int x,int y,int w,int h,int color){assert(w==99 && h==25);keys++;}
static void flush_page(frame_buffer_t *f){}
''' + function(source,'show_room_input') + r'''
int main(void){
 show_room_input("",true,false);assert(keys==12 && hint && confirm && !skip);
 keys=0;hint=false;show_room_input("1234",true,true);assert(keys==12 && !hint);
 keys=0;hint=false;show_room_input("012345",false,false);assert(keys==12 && !hint);
}
''')

    def test_create_join_password_and_open_room_touch_flows(self):
        source = (BASE / 'src/xiaotai_app.c').read_text()
        touch = function(source, 'handle_touch')
        input_branch = touch[touch.index('    if (s_ui_page == UI_PAGE_ROOM_JOIN_CODE'):touch.index('    if (s_ui_page == UI_PAGE_ROOM_LEAVE_CONFIRM)')]
        entry = touch[touch.index('    if (s_ui_page == UI_PAGE_ROOM) {'):touch.index('    if (s_ui_page == UI_PAGE_ROOM &&')]
        inputs = '\n'.join(re.findall(r'^static (?:char|size_t|bool) s_room_(?:code_input\b|code_input_length\b|password_input\b|password_input_length\b|input_create\b)[^;]*;', source, re.M))
        helpers = ''
        for name in ['room_input_render', 'room_input_submit']:
            if re.search(r'static [^\n]+\b' + name + r'\(', source):
                helpers += function(source, name) + '\n'
        run_c(r'''
#include <assert.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#define TAG "test"
#define BK_LOGI(tag,...) do { if (0) printf(__VA_ARGS__); } while (0)
#define BK_ERR_PARAM -1
#define BK_OK 0
#define XIAOTAI_TOUCH_DOWN 1
#define UI_PAGE_HOME 0
#define UI_PAGE_ROOM 1
#define UI_PAGE_ROOM_JOIN_CODE 2
#define UI_PAGE_ROOM_PASSWORD 3
#define XIAOTAI_ROOM_ACTION_CREATE 1
#define XIAOTAI_ROOM_ACTION_JOIN 2
static int s_ui_page=UI_PAGE_ROOM,s_room,requests,last_action,response;
static bool s_touch_action_consumed;
static char request_body[80];
typedef struct {bool assigned,request_pending;} xiaotai_room_snapshot_t;
typedef struct {unsigned x,y,input_event;} control_event_t;
static void navigate_to(int page){s_ui_page=page;}
static void room_render(void){}
static void return_home_from_room(void){s_ui_page=UI_PAGE_HOME;}
static void xiaotai_ui_show_room_join_code(const char *s){(void)s;}
static void xiaotai_ui_show_room_password(bool create,const char *s){(void)create;(void)s;}
static void xiaotai_room_snapshot(int *r,xiaotai_room_snapshot_t *s){(void)r;*s=(xiaotai_room_snapshot_t){0};}
static int xiaotai_room_action(int *r,int action,const char *body){
 (void)r;requests++;last_action=action;snprintf(request_body,sizeof(request_body),"%s",body);return response;
}
''' + '#include "' + str(BASE / 'include/xiaotai_ui_hit_test.h') + '"\n' + (BASE / 'src/xiaotai_ui_hit_test.c').read_text().replace('#include "xiaotai_ui_hit_test.h"', '') +
              inputs + '\n' + helpers + '\nstatic void touch(const control_event_t *event){\n' + input_branch + entry +
              r'''}
static void press(unsigned x,unsigned y){control_event_t e={x,y,1};touch(&e);}
static void digit(unsigned d){
 if(d==0)press(150,165);else press(((d-1)%3)*107+50,64+((d-1)/3)*29+12);
}
static void submit(void){press(270,165);}
int main(void){
 /* Create choices share a row; join has its own row. Gaps do nothing. */
 assert(xiaotai_ui_room_entry_action(80,150)==XIAOTAI_UI_ACTION_ROOM_CREATE);
 assert(xiaotai_ui_room_entry_action(240,150)==XIAOTAI_UI_ACTION_ROOM_CREATE_PASSWORD);
 assert(xiaotai_ui_room_entry_action(80,205)==XIAOTAI_UI_ACTION_ROOM_JOIN);
 assert(xiaotai_ui_room_entry_action(240,205)==XIAOTAI_UI_ACTION_ROOM_JOIN);
 assert(xiaotai_ui_room_entry_action(160,150)==XIAOTAI_UI_ACTION_NONE);
 assert(xiaotai_ui_room_entry_action(4,205)==XIAOTAI_UI_ACTION_NONE);

 /* Actual new entry hit box, leading-zero password, and bounded input. */
 press(240,150);assert(s_ui_page==UI_PAGE_ROOM_PASSWORD && requests==0);
 digit(0);digit(1);submit();assert(requests==0);
 digit(2);digit(3);digit(9);submit();
 assert(last_action==1 && strcmp(request_body,"{\"password\":\"0123\"}")==0);
 assert(s_ui_page==UI_PAGE_ROOM);
 /* Join submits only after the separate password page. */
 press(160,205);digit(0);digit(1);digit(2);digit(3);digit(4);digit(5);submit();
 assert(s_ui_page==UI_PAGE_ROOM_PASSWORD && requests==1);
 digit(0);digit(0);digit(0);digit(7);press(40,165);digit(8);submit();
 assert(last_action==2 && strcmp(request_body,"{\"room_code\":\"012345\",\"password\":\"0008\"}")==0);
 /* Open join uses the same confirm; incomplete passwords cannot be skipped. */
 press(160,205);digit(1);digit(2);digit(3);digit(4);digit(5);digit(6);submit();
 unsigned before_open=requests;
 digit(9);submit();assert(requests==before_open && s_ui_page==UI_PAGE_ROOM_PASSWORD);
 press(40,165);submit();
 assert(strcmp(request_body,"{\"room_code\":\"123456\",\"password\":\"\"}")==0);
 /* Existing one-touch open creation remains available. */
 press(80,150);assert(last_action==1 && strcmp(request_body,"{\"password\":\"\"}")==0);
 /* Cancelling a password never submits, and back returns to room code. */
 unsigned before=requests;
 press(160,205);digit(1);digit(2);digit(3);digit(4);digit(5);digit(6);submit();
 digit(9);press(10,10);assert(s_ui_page==UI_PAGE_ROOM_JOIN_CODE && requests==before);
 press(10,10);assert(s_ui_page==UI_PAGE_ROOM && requests==before);
 /* Synchronous rejection keeps the form editable. */
 press(240,150);digit(1);digit(2);digit(3);digit(4);response=-1;submit();
 assert(s_ui_page==UI_PAGE_ROOM_PASSWORD);
 return 0;
}
''')

if __name__ == '__main__':
    unittest.main()
