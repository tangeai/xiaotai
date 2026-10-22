"""Exercise BK touch input, preview and persistent settings at production seams."""
import unittest
from test_s3_product_regressions import ROOT, function, run_c
BASE = ROOT / 'firmware/beken/bk7258/lckfb-bk7258-xiaotai/ap'

class BkProductGaps(unittest.TestCase):
    def test_expression_background_never_applies(self):
        source=(BASE/'src/xiaotai_app.c').read_text()
        touch=function(source,'handle_touch')
        a=touch.index('    if (s_ui_page == UI_PAGE_EXPRESSIONS) {')
        b=touch.index('    if (s_ui_page == UI_PAGE_NETWORK ||',a)
        run_c(r'''
#include <assert.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#define UI_PAGE_EXPRESSIONS 1
#define UI_PAGE_EXPRESSION_PREVIEW 2
#define UI_PAGE_HOME 3
#define UI_PAGE_SETTINGS 4
static bool s_settings_save_failed;
static void render_settings(void){}
static int s_ui_page=UI_PAGE_EXPRESSIONS,applied,saves;
static unsigned s_expression_selected,s_expression_page;
static struct {int emotion;} s_ai_view;
static unsigned s_home_last_refresh_ms;
static struct {unsigned expression_index;} s_settings;
static size_t xiaotai_ai_emotion_count(void){return 21;}
static const char *xiaotai_ai_emotion_at(size_t i){return "happy";}
static void xiaotai_ai_view_set_emotion(void *p,const char *e){applied++;}
static void render_expressions(void){}
static void render_expression_preview(void){}
static void navigate_to(int page){s_ui_page=page;}
static void save_settings(void){saves++;}
typedef struct {unsigned x,y;} control_event_t;
static void touch(const control_event_t *event){
''' + touch[a:b] + r'''
}
int main(void){
 control_event_t e={160,150};touch(&e);
 assert(applied==0 && "browsing must not apply an expression");
 (void)s_ai_view;
 for(unsigned i=0;i<21;i++){
  s_ui_page=UI_PAGE_EXPRESSIONS;s_expression_page=i/6;
  e.x=30+(i%2)*156;e.y=50+((i%6)/2)*48;touch(&e);
  assert(s_ui_page==UI_PAGE_EXPRESSION_PREVIEW && s_expression_selected==i);
  e.x=160;e.y=80;touch(&e);assert(saves==0);
 }
 e.x=160;e.y=190;touch(&e);
 assert(s_settings.expression_index==20 && saves==1 && s_ui_page==UI_PAGE_HOME);
 s_ui_page=UI_PAGE_EXPRESSIONS;s_expression_page=3;e.x=280;e.y=210;touch(&e);assert(s_expression_page==3);
 e.x=30;touch(&e);assert(s_expression_page==2);
 s_expression_page=0;touch(&e);assert(s_expression_page==0);
 e.x=160;e.y=80;touch(&e);assert(s_ui_page==UI_PAGE_EXPRESSIONS);
 s_ui_page=UI_PAGE_EXPRESSION_PREVIEW;s_settings_save_failed=true;e.x=160;e.y=190;touch(&e);
 assert(s_ui_page==UI_PAGE_SETTINGS && saves==2);
 assert(applied==0);
}
''')

    def test_contact_pagination_bounds_and_gaps(self):
        source=(BASE/'src/xiaotai_app.c').read_text()
        touch=function(source,'handle_touch')
        a=touch.index('    if (s_ui_page == UI_PAGE_CONTACTS) {')
        b=touch.index('    if (s_ui_page == UI_PAGE_EXPRESSIONS) {',a)
        run_c(r'''
#include <assert.h>
#include <stddef.h>
#include <stdbool.h>
#include <stdint.h>
#define UI_PAGE_CONTACTS 1
#define UI_PAGE_CONTACT_DETAIL 2
#define XIAOTAI_CONTACT_VOIP 1
static int s_ui_page=UI_PAGE_CONTACTS;
static size_t s_contact_display_count=7,s_contact_page,s_contact_selected;
static size_t s_contact_display_indices[8]={0,1,2,3,4,5,6,7};
typedef struct {bool online;int type;} xiaotai_contact_t;
static struct {unsigned count;xiaotai_contact_t entries[8];} s_contacts={.count=7};
static void render_contacts(void){}
static void render_contact_detail(void){}
static void dial_contact(const xiaotai_contact_t *c){}
typedef struct {unsigned x,y;} control_event_t;
static void touch(const control_event_t *event){
''' + touch[a:b] + r'''
}
int main(void){
 control_event_t e={160,218};touch(&e);
 assert(s_contact_page==0 && "footer gap must not change the page");
 e.x=260;e.y=210;touch(&e);assert(s_contact_page==1);
 touch(&e);assert(s_contact_page==2);touch(&e);assert(s_contact_page==2);
 e.x=50;touch(&e);assert(s_contact_page==1);
 s_contact_page=0;e.x=30;e.y=79;touch(&e);assert(s_ui_page==UI_PAGE_CONTACTS);
 e.y=50;touch(&e);assert(s_ui_page==UI_PAGE_CONTACT_DETAIL && s_contact_selected==0);
}
''')

    def test_ai_menu_opens_history_without_replacing_it_with_home(self):
        source=(BASE/'src/xiaotai_app.c').read_text()
        run_c(r'''
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
typedef enum { UI_PAGE_HOME,UI_PAGE_AI_CHAT,UI_PAGE_CONTACTS,UI_PAGE_ROOM,UI_PAGE_EXPRESSIONS,UI_PAGE_SETTINGS,UI_PAGE_DIAGNOSTICS } ui_page_t;
static ui_page_t s_ui_page;
static unsigned s_contact_page,s_contact_selected,s_room_participant_page,s_diagnostics_tab;
static int s_room;
#define XIAOTAI_ROOM_ACTION_SYNC 1
static void navigate_to(ui_page_t p){s_ui_page=p;}
static void render_ai_history(void){}
static void render_contacts(void){}
static void refresh_contacts(void){}
static int xiaotai_room_action(void *r,int a,void *p){return 0;}
static void room_render(void){}
static void render_expressions(void){}
static void render_settings(void){}
static void render_diagnostics(void){}
''' + function(source,'open_launcher_item') + r'''
int main(void){assert(open_launcher_item(0));assert(s_ui_page==UI_PAGE_AI_CHAT);}
''')

    def test_settings_failure_is_retained_until_successful_retry(self):
        source=(BASE/'src/xiaotai_app.c').read_text()
        run_c(r'''
#include <assert.h>
#include <stdbool.h>
#define BK_OK 0
#define BK_LOGE(...) ((void)0)
static bool s_settings_save_failed;
static int s_settings,fail,calls;
static int xiaotai_storage_save_settings(void *s){calls++;return fail?-1:0;}
''' + function(source,'save_settings') + r'''
int main(void){fail=1;save_settings();assert(s_settings_save_failed && calls==1);
 fail=0;save_settings();assert(!s_settings_save_failed && calls==2);}
''')

    def test_history_replaces_partials_and_bounds_storage(self):
        run_c(r'''
#include <assert.h>
#include <stdio.h>
#include "xiaotai_ai_history.h"
int main(void){
 xiaotai_ai_history_t history={0};xiaotai_ai_view_t view={.caption_type=0};
 strcpy(view.utterance_id,"a");strcpy(view.caption,"你");
 xiaotai_ai_history_update(&history,&view,1);
 strcpy(view.caption,"你好");xiaotai_ai_history_update(&history,&view,1);
 assert(history.count==1 && !strcmp(history.entries[0].text,"你好"));
 view.caption_type=1;strcpy(view.caption,"你好呀");xiaotai_ai_history_update(&history,&view,1);
 assert(history.count==2 && history.entries[1].speaker==1);
 xiaotai_ai_history_update(&history,&view,2);assert(history.count==3);
 view.caption_type=-1;xiaotai_ai_history_update(&history,&view,2);
 view.caption_type=1;xiaotai_ai_history_update(&history,&view,2);assert(history.count==4);
 for(unsigned i=0;i<30;i++){
  snprintf(view.utterance_id,sizeof(view.utterance_id),"id%u",i);
  snprintf(view.caption,sizeof(view.caption),"text%u",i);
  xiaotai_ai_history_update(&history,&view,2);
 }
 assert(history.count==XIAOTAI_AI_HISTORY_CAPACITY);
 assert(!strcmp(history.entries[0].text,"text18"));
 assert(!strcmp(history.entries[11].text,"text29"));
 xiaotai_ai_history_update(NULL,&view,1);
}
''', include_dirs=(ROOT/'product/include',))

    def test_history_renderer_keeps_full_ascii_and_utf8_above_footer(self):
        source=(BASE/'src/xiaotai_ui.c').read_text()
        run_c(r'''
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
typedef int frame_buffer_t;
static int frame,rows;
static char rendered[512];
static frame_buffer_t *page_frame(const char *s){assert(!strcmp(s,"AI 聊天"));return &frame;}
static void mixed_text(frame_buffer_t *f,int x,int y,const char *s,int scale,int color){
 if(y>=62){assert(y+16<=190);strcat(rendered,s);rows++;}
}
static void rectangle(frame_buffer_t *f,int x,int y,int w,int h,int c){assert(y>=196);}
static void centered_mixed_in(frame_buffer_t*f,int x,int y,const char*s,int scale,int color,int width){}
static void centered_mixed_text(frame_buffer_t*f,int y,const char*s,int scale,int color){}
static void flush_page(frame_buffer_t*f){}
''' + function(source,'xiaotai_ui_show_ai_history') + r'''
int main(void){
 char text[256];memset(text,'a',255);text[255]=0;
 xiaotai_ui_show_ai_history(text,true,0,1,true);
 assert(!strcmp(rendered,text) && rows==8);
 rendered[0]=0;rows=0;text[0]=0;
 for(int i=0;i<80;i++)strcat(text,"你");
 xiaotai_ui_show_ai_history(text,false,0,1,true);assert(!strcmp(rendered,text) && rows<=8);
 xiaotai_ui_show_ai_history(NULL,false,0,0,false);
}
''')

    def test_caption_event_preserves_selected_page(self):
        source=(BASE/'src/xiaotai_app.c').read_text()
        run_c(r'''
#include <assert.h>
#include <stdio.h>
#include "xiaotai_ai_history.h"
#define XIAOTAI_OWNER_AI 1
#define UI_PAGE_HOME 0
#define UI_PAGE_AI_CHAT 1
#define UI_PAGE_SETTINGS 2
#define BK_LOGI(...) ((void)0)
static int s_runtime_mutex,face_renders,history_renders,s_ui_page;
static struct {int owner;uint32_t generation;} s_runtime={1,7};
static xiaotai_ai_view_t s_ai_view;
static xiaotai_ai_history_t history;
static xiaotai_ai_history_t *s_ai_history=&history;
static unsigned s_ai_history_page;
typedef struct {uint32_t generation;char *text;} control_event_t;
static void rtos_lock_mutex(int*m){}
static void rtos_unlock_mutex(int*m){}
static unsigned rtos_get_time(void){return 1000;}
static bool xiaotai_runtime_has_incoming(void*r){return false;}
static void xiaotai_ui_show_ai(int p,const char*e,const char*t,bool a,bool tag){face_renders++;}
static void render_ai_history(void){history_renders++;}
bool xiaotai_ai_view_apply(xiaotai_ai_view_t*v,const char*j,uint32_t now){
 v->caption_type=1;strcpy(v->caption,j);strcpy(v->utterance_id,"a");return true;
}
''' + function(source,'handle_ai_ui') + r'''
int main(void){
 control_event_t e={7,"你好"};s_ui_page=UI_PAGE_SETTINGS;handle_ai_ui(&e);
 assert(face_renders==0 && history_renders==0 && history.count==1);
 s_ui_page=UI_PAGE_AI_CHAT;handle_ai_ui(&e);assert(history_renders==1 && face_renders==0);
 s_ui_page=UI_PAGE_HOME;handle_ai_ui(&e);assert(face_renders==1);
 e.generation=6;handle_ai_ui(&e);assert(face_renders==1);
}
''', include_dirs=(ROOT/'product/include',))

if __name__=='__main__':unittest.main()
