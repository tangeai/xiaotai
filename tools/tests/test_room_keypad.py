"""Click production-rendered room keys through the ESP UI action dispatcher."""
import re
import unittest

from test_s3_product_regressions import ROOT, function, run_c


class RoomKeypad(unittest.TestCase):
    def test_rendered_keys_preserve_digits_and_room_flow(self):
        source = (ROOT / 'platforms/esp-idf/components/starter_product/src/starter_product.c').read_text()
        enums = '\n'.join(re.search(r'typedef enum \{[^}]+\} ' + name + ';', source).group()
                          for name in ('product_page_t', 'product_action_t'))
        handler = function(source, 'on_action')
        start = handler.index('action == ACTION_ROOM) {')
        end = handler.index('} else if (action >= ACTION_DIAG_SYSTEM', start)
        dispatch = 'static void dispatch(product_action_t action) { if (' + handler[start:end] + '} }'
        run_c(r'''
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
    click("确定"); click("无密码");
    assert(joins==2 && strcmp(joined_code,"888888")==0 && joined_password[0]==0);
    dispatch(ACTION_ROOM_CREATE_PASSWORD); click("1"); click("2"); click("3"); click("4");
    click("确定"); assert(creates==1 && s_page==PAGE_ROOM);
    dispatch(ACTION_ROOM_LEAVE); assert(s_page==PAGE_ROOM_LEAVE_CONFIRM && leaves==0);
    dispatch(ACTION_ROOM_LEAVE_CANCEL); assert(s_page==PAGE_ROOM && leaves==0);
    dispatch(ACTION_ROOM_LEAVE); dispatch(ACTION_ROOM_LEAVE_CONFIRM); assert(leaves==1);
}
''')


if __name__ == '__main__':
    unittest.main()
