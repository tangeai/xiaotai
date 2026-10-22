"""BK contacts entry must request current platform online status each time."""

import unittest

from test_s3_product_regressions import ROOT, function, run_c


APP = ROOT / 'platforms/beken/runtime/src/xiaotai_app.c'


class BKContactsRefreshOnEntry(unittest.TestCase):
    def test_opening_contacts_again_requests_fresh_online_status(self):
        source = APP.read_text()
        run_c(r'''
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
typedef enum {
    UI_PAGE_HOME, UI_PAGE_AI_CHAT, UI_PAGE_CONTACTS, UI_PAGE_ROOM,
    UI_PAGE_EXPRESSIONS, UI_PAGE_SETTINGS, UI_PAGE_DIAGNOSTICS
} ui_page_t;
static ui_page_t s_ui_page;
static uint8_t s_contact_page;
static unsigned s_contact_selected,s_room_participant_page,s_diagnostics_tab;
static int s_room,requests,renders;
#define XIAOTAI_ROOM_ACTION_SYNC 1
static void navigate_to(ui_page_t page){s_ui_page=page;}
static void render_ai_history(void){}
static void render_contacts(void){renders++;}
static void refresh_contacts(void){requests++;}
static int xiaotai_room_action(int *room,int action,void *arg){
    (void)room;(void)action;(void)arg;return 0;
}
static void room_render(void){}
static void render_expressions(void){}
static void render_settings(void){}
static void render_diagnostics(void){}
''' + function(source, 'open_launcher_item') + r'''
int main(void){
    assert(!open_launcher_item(1));
    assert(s_ui_page==UI_PAGE_CONTACTS && requests==1 && renders==1);
    assert(open_launcher_item(0));
    assert(!open_launcher_item(1));
    assert(s_ui_page==UI_PAGE_CONTACTS && requests==2 && renders==2);
}
''')


if __name__ == '__main__':
    unittest.main()
