"""Regression replay using production functions and controlled dependencies."""
import unittest
import sys
sys.path.insert(0,'tools/tests')
from test_s3_product_regressions import function,run_c
from pathlib import Path
s=Path('platforms/beken/runtime/src/xiaotai_app.c').read_text()
h=function(s,'handle_touch'); branch=h[h.index('    if (s_ui_page == UI_PAGE_CONTACT_DETAIL)'):h.index('    if (s_ui_page == UI_PAGE_EXPRESSIONS)')]

class Regression(unittest.TestCase):
    def test_recovery(self):
        run_c(r'''
        #include <stdio.h>
        #include <stdbool.h>
        #include <string.h>
        #include <assert.h>
        #include "xiaotai_contacts.h"
        #define BK_LOGW(...) ((void)0)
        #define BK_LOGI(...) ((void)0)
        #define UI_PAGE_CONTACTS 1
        #define UI_PAGE_CONTACT_DETAIL 2
        #define UI_PAGE_WECHAT_QR 3
        #define UI_PAGE_HOME 4
        #define XIAOTAI_OWNER_NONE 0
        typedef struct {char *text;int x,y;} control_event_t;
        static xiaotai_contacts_t s_contacts;
        static xiaotai_contact_t s_contact_target;
        static unsigned s_contact_selected;
        static int s_ui_page=UI_PAGE_CONTACT_DETAIL,s_runtime_mutex;
        static bool s_qr_waiting_for_wechat;
        static struct {int owner;} s_runtime;
        static char screen[65],target[65];
        static void render_contacts(void){}
        static void xiaotai_ui_show_contact_detail(const char*n,bool o,bool v){strcpy(screen,n);}
        static void dial_contact(const xiaotai_contact_t*c){strcpy(target,c->id);}
        /* Boundary fake: a successful asynchronous contacts response reorders index 0. */
        int xiaotai_contacts_replace_all_json(xiaotai_contacts_t*c,const char*j){strcpy(c->entries[0].id,"Bob");strcpy(c->entries[0].name,"Bob");return 1;}
        xiaotai_contact_match_t xiaotai_contacts_first_of_type(const xiaotai_contacts_t*c,xiaotai_contact_type_t t,xiaotai_contact_t*r){return XIAOTAI_CONTACT_MATCH_OK;}
        static void rtos_lock_mutex(int*m){}
        static void rtos_unlock_mutex(int*m){}
        static void quick_call_first_wechat_contact(void){}
        static void quick_call_first_contact(void){}
        ''' + function(s,'render_contact_detail')+function(s,'handle_contacts')+'\nstatic void handle_detail_touch(control_event_t *event){\n'+branch+'\n}\n'+r'''
        int main(void){
         s_contacts.count=1;s_contacts.entries[0].online=true;
         strcpy(s_contacts.entries[0].id,"Alice");strcpy(s_contacts.entries[0].name,"Alice");
         render_contact_detail();control_event_t e={.text="new response",.x=100,.y=210};
         handle_contacts(&e);handle_detail_touch(&e);
         printf("BK displayed contact: %s; dial target: %s\n",screen,target);
         assert(!strcmp(screen,"Alice") && target[0]==0);
         s_contacts.count=2;s_contacts.entries[1]=s_contact_target;
         s_ui_page=UI_PAGE_CONTACT_DETAIL;handle_detail_touch(&e);
         assert(!strcmp(target,"Alice"));
        }
        ''', [Path('product/include')])

if __name__ == "__main__": unittest.main()
