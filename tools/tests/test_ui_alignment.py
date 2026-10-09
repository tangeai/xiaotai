"""Replay shipped UI renderers, including geometry and room list updates."""
import re
import unittest
from test_s3_product_regressions import ROOT, function, run_c
from test_s3_room_refresh import run_room_case

ESP = ROOT / 'platforms/esp-idf/components/starter_product/src/starter_product.c'


class UiAlignment(unittest.TestCase):
    def test_bk_settings_touch_adjusts_only_selected_value(self):
        base = ROOT / 'firmware/beken/bk7258/lckfb-bk7258-xiaotai/ap'
        source = (base / 'src/xiaotai_app.c').read_text()
        touch = function(source, 'handle_touch')
        branch = touch[touch.index('    if (s_ui_page == UI_PAGE_SETTINGS) {'):]
        run_c(r'''
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
''' + '#include "' + str(base / 'include/xiaotai_ui_hit_test.h') + '"\n' +
              (base / 'src/xiaotai_ui_hit_test.c').read_text().replace('#include "xiaotai_ui_hit_test.h"', '') + r'''
#define UI_PAGE_SETTINGS 1
#define UI_PAGE_NETWORK 2
#define XIAOTAI_MIC_SENSITIVITY_MAX 5
#define TAG "test"
#define BK_LOGE(...) ((void)0)
typedef struct {unsigned x,y;} control_event_t;
static int s_ui_page=UI_PAGE_SETTINGS, saved, volume_calls, sensitivity_calls, rejected, reset_requests;
static struct {uint8_t volume,microphone_sensitivity,screen_timeout_index;bool speaker_muted,microphone_muted;} s_settings={5,4,0,false,false};
static void xiaotai_audio_set_volume(unsigned v){volume_calls++;}
static void xiaotai_audio_set_speaker_muted(bool v){}
static void xiaotai_audio_set_microphone_muted(bool v){}
static int apply_microphone_sensitivity(unsigned v,void *p){return 0;}
static bool xiaotai_audio_commit_sensitivity(uint8_t *value,uint8_t next,int (*f)(unsigned,void*),void *p){
 sensitivity_calls++;if(rejected)return false;*value=next;return true;
}
static void xiaotai_ui_show_status(const char *s){}
static int xiaotai_storage_save_settings(void *s){saved++;return 0;}
static void render_settings(void){}
static void render_network(void){}
static void handle_reset_action(unsigned action){assert(action==0);reset_requests++;}
static void navigate_to(int page){s_ui_page=page;}
static void touch(const control_event_t *event){
''' + branch + r'''
static void press(unsigned x,unsigned y){control_event_t e={x,y};touch(&e);}
int main(void){
 press(160,96);assert(saved==0 && sensitivity_calls==0);
 press(290,96);assert(saved==1 && s_settings.microphone_sensitivity==5 && volume_calls==0);
 press(290,96);assert(saved==1 && s_settings.microphone_sensitivity==5);
 for(int i=0;i<8;i++)press(24,96);
 assert(s_settings.microphone_sensitivity==1 && saved==5);
 rejected=1;press(290,96);assert(s_settings.microphone_sensitivity==1 && saved==5);
 press(24,54);assert(s_settings.volume==4 && volume_calls==1 && saved==6);
 press(160,132);assert(!s_settings.speaker_muted && !s_settings.microphone_muted && saved==6);
 press(40,132);assert(s_settings.speaker_muted && !s_settings.microphone_muted);
 press(230,132);assert(s_settings.microphone_muted);
 press(40,172);assert(s_settings.screen_timeout_index==1);
 press(230,210);assert(reset_requests==1 && s_ui_page==UI_PAGE_SETTINGS);
 press(160,210);assert(reset_requests==1);
 press(40,210);assert(s_ui_page==UI_PAGE_NETWORK);
}
''')

    def test_single_page_and_large_room_keep_ptt_widget(self):
        run_room_case(r'''
 snapshot.room_online_count=2;product=snapshot;refresh_tick();
 assert(shown("已显示全部成员") && !shown("缓存"));
 assert(!s_room_previous_button->visible && !s_room_next_button->visible);
 lv_obj_t *ptt=s_room_ptt_button;
 snapshot.room_member_count=8;snapshot.room_online_count=100;
 for(int i=0;i<8;i++) snprintf(snapshot.room_members[i].id,65,"member%d",i);
 s_room_members_page=2;product=snapshot;refresh_tick();
 assert(shown("3/3 页") && shown("显示 8/100 人"));
 assert(s_room_previous_button->visible && !s_room_previous_button->disabled);
 assert(s_room_next_button->visible && s_room_next_button->disabled);
 assert(shown("member6") && !shown("member0"));
 snapshot.room_members[0].speaking=true;product=snapshot;refresh_tick();
 assert(s_room_members_page==2 && s_room_ptt_button==ptt && nodes==initial_nodes);
 snapshot.room_member_count=1;snapshot.room_online_count=1;product=snapshot;refresh_tick();
 assert(s_room_members_page==0 && shown("已显示全部成员"));
''')

    def test_settings_groups_adjustments_before_switches(self):
        source = ESP.read_text()
        enums = re.search(r'typedef enum \{[^}]+\} product_action_t;', source).group()
        run_c(r'''
#include "/home/workspace/xiaotai/product/include/xiaotai_ui_copy.h"
#include <assert.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
typedef struct {char text[128];int x,y,w,h;} lv_obj_t;
''' + enums + r'''
#define lv_color_hex(c) (c)
static struct {unsigned volume,microphone_sensitivity,sleep_index;bool speaker_muted,microphone_muted,acknowledgement_male;} s_preferences;
static const char *s_sleep_names[]={"5 分钟"};
static lv_obj_t pool[24];static unsigned count;
static lv_obj_t *s_settings_volume,*s_settings_microphone_sensitivity,*s_settings_speaker,*s_settings_microphone,*s_settings_sleep,*s_settings_acknowledgement;
static void make_header_back_button(lv_obj_t *o,product_action_t a){}
static void render_header(lv_obj_t *o,const char *t){}
static lv_obj_t *make_button(lv_obj_t *o,const char *t,int x,int y,int w,int h,product_action_t a){
 lv_obj_t *n=&pool[count++];snprintf(n->text,sizeof(n->text),"%s",t);n->x=x;n->y=y;n->w=w;n->h=h;return n;
}
static lv_obj_t *make_settings_stepper(lv_obj_t *o,const char *t,int y,product_action_t a,product_action_t b){return make_button(o,t,0,y,320,38,a);}
static void lv_label_set_text(lv_obj_t *o,const char *t){strcpy(o->text,t);}
static void set_button_text(lv_obj_t *o,const char *t){strcpy(o->text,t);}
#define PAGE_SETTINGS 1
static int s_page=PAGE_SETTINGS;
''' + function(source, 'render_settings') + function(source, 'refresh_settings_controls') + r'''
int main(void){
 s_preferences.volume=9;s_preferences.microphone_sensitivity=4;
 render_settings(NULL);
 assert(strstr(s_settings_volume->text,"扬声器音量"));
 assert(strstr(s_settings_microphone_sensitivity->text,"麦克风灵敏度"));
 assert(s_settings_volume->y<s_settings_microphone_sensitivity->y);
 assert(s_settings_microphone_sensitivity->y+38<=s_settings_speaker->y);
 assert(s_settings_speaker->y==s_settings_microphone->y && s_settings_speaker->h==s_settings_microphone->h);
 s_preferences.volume=0;s_preferences.microphone_sensitivity=1;refresh_settings_controls();
 assert(strstr(s_settings_volume->text,"扬声器音量") && strstr(s_settings_volume->text,"0 / 10"));
 assert(strstr(s_settings_microphone_sensitivity->text,"麦克风灵敏度") && strstr(s_settings_microphone_sensitivity->text,"1 / 5"));
}
''')


if __name__ == '__main__':
    unittest.main()
