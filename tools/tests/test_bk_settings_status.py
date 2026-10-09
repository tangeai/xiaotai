"""Replay BK settings geometry, reset confirmation and storage boundary."""
import unittest
from test_s3_product_regressions import ROOT, function, run_c

BASE = ROOT / 'firmware/beken/bk7258/lckfb-bk7258-xiaotai/ap'

class BkSettingsStatus(unittest.TestCase):
    def test_settings_buttons_form_complete_rows(self):
        source = (BASE / 'src/xiaotai_ui.c').read_text()
        run_c(r'''
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "/home/workspace/xiaotai/product/include/xiaotai_ui_copy.h"
typedef int frame_buffer_t;
static int frame, rectangles;
static bool reset, network, sleep_full;
static frame_buffer_t *page_frame(const char *s){return &frame;}
static void rectangle(frame_buffer_t *f,int x,int y,int w,int h,int color){
 if(y==40 || y==82){assert((x==8 || x==270) && w==42 && h==38);}
 if(y==160){assert(x==8 && w==304 && h==30);sleep_full=true;}
 if(y==196){assert((x==8 || x==164) && w==148 && h==30);rectangles++;}
}
static void centered_mixed_in(frame_buffer_t *f,int x,int y,const char *s,int scale,int color,int w){
 if(!strcmp(s,"重置设备")){assert(x==238 && y==203);reset=true;}
 if(!strcmp(s,"网络信息")){assert(x==82 && y==203);network=true;}
}
static void flush_page(frame_buffer_t *f){}
''' + function(source, 'xiaotai_ui_show_settings') + r'''
int main(void){xiaotai_ui_show_settings(8,false,false,5,"从不");assert(reset && network && sleep_full && rectangles==2);}
''')

    def test_reset_confirmation_cancel_failure_retry_and_duplicate(self):
        source = (BASE / 'src/xiaotai_app.c').read_text()
        run_c(r'''
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#define BK_OK 0
#define UI_PAGE_SETTINGS 1
#define UI_PAGE_RESET_CONFIRM 2
static int s_ui_page, s_reset_status, cleared, restarted, failed;
static void render_settings(void){}
static void navigate_to(int p){s_ui_page=p;}
static void xiaotai_ui_show_reset_confirmation(int status){s_reset_status=status;}
static int xiaotai_storage_reset_user_data(void){cleared++;return failed ? -1 : 0;}
static void rtos_delay_milliseconds(unsigned t){}
static void bk_reboot(void){restarted++;}
''' + function(source, 'handle_reset_action') + r'''
int main(void){
 handle_reset_action(0);assert(s_ui_page==UI_PAGE_RESET_CONFIRM && cleared==0);
 handle_reset_action(1);assert(s_ui_page==UI_PAGE_SETTINGS && cleared==0);
 handle_reset_action(0);failed=1;handle_reset_action(2);
 assert(s_reset_status==2 && restarted==0 && cleared==1);
 failed=0;handle_reset_action(2);assert(cleared==2 && restarted==1);
 handle_reset_action(2);handle_reset_action(1);assert(cleared==2 && restarted==1 && s_ui_page==UI_PAGE_RESET_CONFIRM);
}
''')

    def test_status_events_are_bounded_and_snapshot_is_read_only(self):
        source = (BASE / 'src/xiaotai_metrics.c').read_text()
        run_c(r'''
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "/home/workspace/xiaotai/firmware/beken/bk7258/lckfb-bk7258-xiaotai/ap/include/xiaotai_metrics.h"
static bool s_state_mutex_ready;
static int s_state_mutex, locked;
static char s_state[24]="BOOT";
static xiaotai_metrics_snapshot_t s_snapshot;
static struct {uint32_t at_ms;char state[24];} s_events[7];
static unsigned s_event_count,s_event_next;
static unsigned rtos_get_time(void){return 10000;}
static void rtos_lock_mutex(int *m){assert(!locked);locked=1;}
static void rtos_unlock_mutex(int *m){assert(locked);locked=0;}
''' + function(source, 'xiaotai_metrics_set_state') + function(source, 'xiaotai_metrics_snapshot') + function(source, 'xiaotai_metrics_copy_events') + r'''
int main(void){
 xiaotai_metrics_snapshot_t out;xiaotai_metrics_snapshot(&out);assert(!out.sampled);
 s_state_mutex_ready=true;s_snapshot.sampled=true;s_snapshot.cpu_percent=37;s_snapshot.task_count=27;
 xiaotai_metrics_set_state("READY");xiaotai_metrics_set_state("READY");assert(s_event_count==1);
 for(int i=0;i<12;i++){char state[24];snprintf(state,24,"STATE%d",i);xiaotai_metrics_set_state(state);}
 assert(s_event_count==7);xiaotai_metrics_snapshot(&out);
 assert(out.cpu_percent==37 && out.task_count==27 && !strcmp(out.state,"STATE11"));
 char events[256];xiaotai_metrics_copy_events(events,sizeof(events));
 assert(strstr(events,"STATE11") && strstr(events,"STATE5") && !strstr(events,"STATE4"));
 struct {char text[4];char guard;} small={{0},42};
 xiaotai_metrics_copy_events(small.text,sizeof(small.text));assert(small.text[3]==0 && small.guard==42 && !locked);
}
''')

    def test_child_page_return_and_confirmation_gaps(self):
        source = (BASE / 'src/xiaotai_app.c').read_text()
        touch = function(source, 'handle_touch')
        start = touch.index('    if (s_ui_page == UI_PAGE_RESET_CONFIRM) {')
        end = touch.index('    if (event->y < 40U && s_ui_page != UI_PAGE_HOME)', start)
        run_c(r'''
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#define UI_PAGE_RESET_CONFIRM 1
#define UI_PAGE_SETTINGS 2
#define UI_PAGE_NETWORK 3
#define UI_PAGE_DIAGNOSTICS 4
typedef struct {unsigned x,y;} control_event_t;
static int s_ui_page,s_reset_status,last_action=-1,actions,rendered;
static unsigned s_diagnostics_tab;
static void handle_reset_action(unsigned action){last_action=action;actions++;}
static void navigate_to(int page){s_ui_page=page;}
static void render_settings(void){rendered++;}
static void render_diagnostics(void){rendered++;}
static void touch(const control_event_t *event){
''' + touch[start:end] + r'''
}
static void press(unsigned x,unsigned y){control_event_t e={x,y};touch(&e);}
int main(void){
 s_ui_page=UI_PAGE_RESET_CONFIRM;press(160,200);press(319,200);assert(actions==0);
 press(230,200);assert(last_action==2 && actions==1);
 press(30,200);assert(last_action==1 && actions==2);
 s_reset_status=1;press(230,200);press(20,20);assert(actions==2);
 s_ui_page=UI_PAGE_NETWORK;press(20,20);assert(s_ui_page==UI_PAGE_SETTINGS && rendered==1);
 s_ui_page=UI_PAGE_DIAGNOSTICS;press(160,50);assert(s_diagnostics_tab==1 && rendered==2);
 press(210,50);assert(rendered==2);press(250,50);assert(s_diagnostics_tab==2 && rendered==3);
}
''')

    def test_reset_deletes_only_user_keys_and_reports_partial_failure(self):
        source = (BASE / 'src/xiaotai_storage.c').read_text()
        run_c(r'''
#include <assert.h>
#include <string.h>
#include <stdatomic.h>
#include <stdbool.h>
static atomic_bool s_storage_reset_pending;
static atomic_uint s_storage_writers;
#define BK_OK 0
#define BK_FAIL -1
#define EF_NO_ERR 0
#define EF_ENV_NAME_ERR 1
#define XIAOTAI_DEVICE_KEY "xiaotai_device_v1"
#define XIAOTAI_WIFI_KEY "xiaotai_wifi_v1"
#define XIAOTAI_SETTINGS_KEY "xiaotai_settings_v1"
typedef int EfErrCode;
static int calls, fail_at, missing;
static EfErrCode ef_del_env(const char *key){
 assert(!strcmp(key,XIAOTAI_DEVICE_KEY) || !strcmp(key,XIAOTAI_WIFI_KEY) || !strcmp(key,XIAOTAI_SETTINGS_KEY));
 calls++;return fail_at==calls ? -1 : missing ? EF_ENV_NAME_ERR : EF_NO_ERR;
}
''' + function(source, 'xiaotai_storage_reset_user_data') + r'''
int main(void){
 atomic_store(&s_storage_writers,1);assert(xiaotai_storage_reset_user_data()==BK_FAIL && calls==0);
 atomic_store(&s_storage_writers,0);
 assert(xiaotai_storage_reset_user_data()==BK_OK && calls==3);
 atomic_store(&s_storage_reset_pending,false);calls=0;fail_at=2;assert(xiaotai_storage_reset_user_data()==BK_FAIL && calls==3);
 calls=0;fail_at=0;missing=1;assert(xiaotai_storage_reset_user_data()==BK_OK && calls==3);
}
''')

if __name__ == '__main__':
    unittest.main()
