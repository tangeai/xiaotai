#include <assert.h>
#include <stdint.h>

#include "xiaotai_ui_hit_test.h"

int main(void)
{
    assert(xiaotai_ui_home_action(285U, 219U) ==
           XIAOTAI_UI_ACTION_HOME_MENU);
    assert(xiaotai_ui_home_action(285U, 170U) ==
           XIAOTAI_UI_ACTION_HOME_QUICK_CALL);
    assert(xiaotai_ui_home_action(60U, 215U) ==
           XIAOTAI_UI_ACTION_HOME_AI);
    assert(xiaotai_ui_home_action(285U, 196U) ==
           XIAOTAI_UI_ACTION_HOME_QUICK_CALL);
    assert(xiaotai_ui_home_action(285U, 198U) ==
           XIAOTAI_UI_ACTION_HOME_MENU);
    assert(xiaotai_ui_home_action(160U, 120U) ==
           XIAOTAI_UI_ACTION_HOME_AI);
    assert(xiaotai_ui_action_triggers_on_down(
               XIAOTAI_UI_ACTION_HOME_QUICK_CALL));
    assert(!xiaotai_ui_action_triggers_on_down(XIAOTAI_UI_ACTION_HOME_AI));

    assert(xiaotai_ui_incoming_action(60U, 210U) ==
           XIAOTAI_UI_ACTION_CALL_REJECT);
    assert(xiaotai_ui_incoming_action(260U, 210U) ==
           XIAOTAI_UI_ACTION_CALL_ACCEPT);
    assert(xiaotai_ui_incoming_action(160U, 120U) ==
           XIAOTAI_UI_ACTION_NONE);

    assert(xiaotai_ui_active_call_action(60U, 210U) ==
           XIAOTAI_UI_ACTION_CALL_MIC_TOGGLE);
    assert(xiaotai_ui_active_call_action(260U, 210U) ==
           XIAOTAI_UI_ACTION_CALL_HANGUP);
    assert(xiaotai_ui_active_call_action(160U, 120U) ==
           XIAOTAI_UI_ACTION_NONE);

    assert(xiaotai_ui_room_action(240U, 208U, true) ==
           XIAOTAI_UI_ACTION_ROOM_TALK_START);
    assert(xiaotai_ui_room_action(240U, 208U, false) ==
           XIAOTAI_UI_ACTION_ROOM_TALK_STOP);
    assert(xiaotai_ui_room_action(56U, 208U, true) ==
           XIAOTAI_UI_ACTION_ROOM_LEAVE);
    assert(xiaotai_ui_room_action(56U, 208U, false) ==
           XIAOTAI_UI_ACTION_NONE);
    assert(xiaotai_ui_room_action(160U, 208U, true) ==
           XIAOTAI_UI_ACTION_NONE);
    assert(xiaotai_ui_room_action(160U, 208U, false) ==
           XIAOTAI_UI_ACTION_NONE);
    assert(xiaotai_ui_room_action(30U, 127U, true) ==
           XIAOTAI_UI_ACTION_NONE);
    assert(xiaotai_ui_room_action(160U, 127U, true) ==
           XIAOTAI_UI_ACTION_NONE);
    assert(xiaotai_ui_room_action(56U, 166U, true) ==
           XIAOTAI_UI_ACTION_NONE);
    assert(xiaotai_ui_room_action(56U, 166U, false) ==
           XIAOTAI_UI_ACTION_ROOM_PAGE_PREV);
    assert(xiaotai_ui_room_action(264U, 166U, true) ==
           XIAOTAI_UI_ACTION_NONE);
    assert(xiaotai_ui_room_action(264U, 166U, false) ==
           XIAOTAI_UI_ACTION_ROOM_PAGE_NEXT);
    assert(xiaotai_ui_room_action(160U, 166U, false) ==
           XIAOTAI_UI_ACTION_NONE);
    assert(xiaotai_ui_room_page_count(0U) == 1U);
    assert(xiaotai_ui_room_page_count(3U) == 1U);
    assert(xiaotai_ui_room_page_count(4U) == 2U);
    assert(xiaotai_ui_room_page_count(8U) == 3U);
    assert(xiaotai_ui_room_page_clamp(7U, 8U) == 2U);
    assert(xiaotai_ui_room_page_clamp(2U, 3U) == 0U);
    assert(xiaotai_ui_room_action(16U, 17U, true) ==
           XIAOTAI_UI_ACTION_BACK);
    assert(xiaotai_ui_room_action(16U, 17U, false) ==
           XIAOTAI_UI_ACTION_NONE);
    assert(xiaotai_ui_action_triggers_on_down(XIAOTAI_UI_ACTION_BACK));
    assert(xiaotai_ui_action_triggers_on_down(
               XIAOTAI_UI_ACTION_ROOM_LEAVE));

    assert(xiaotai_ui_room_entry_action(60U, 208U) ==
           XIAOTAI_UI_ACTION_ROOM_CREATE);
    assert(xiaotai_ui_room_entry_action(260U, 208U) ==
           XIAOTAI_UI_ACTION_ROOM_JOIN);
    assert(xiaotai_ui_room_entry_action(160U, 120U) ==
           XIAOTAI_UI_ACTION_NONE);
    assert(xiaotai_ui_room_entry_action(16U, 17U) ==
           XIAOTAI_UI_ACTION_BACK);
    assert(xiaotai_ui_room_keypad_key(45U, 70U) == 1);
    assert(xiaotai_ui_room_keypad_key(160U, 127U) == 8);
    assert(xiaotai_ui_room_keypad_key(160U, 163U) == 0);
    assert(xiaotai_ui_room_keypad_key(45U, 163U) ==
           XIAOTAI_UI_ROOM_KEY_DELETE);
    assert(xiaotai_ui_room_keypad_key(160U, 210U) ==
           XIAOTAI_UI_ROOM_KEY_SUBMIT);

    assert(xiaotai_ui_room_leave_confirm_action(60U, 208U) ==
           XIAOTAI_UI_ACTION_ROOM_LEAVE_CANCEL);
    assert(xiaotai_ui_room_leave_confirm_action(260U, 208U) ==
           XIAOTAI_UI_ACTION_ROOM_LEAVE_CONFIRM);
    assert(xiaotai_ui_room_leave_confirm_action(160U, 120U) ==
           XIAOTAI_UI_ACTION_NONE);
    assert(xiaotai_ui_room_leave_confirm_action(16U, 17U) ==
           XIAOTAI_UI_ACTION_ROOM_LEAVE_CANCEL);

    assert(xiaotai_ui_settings_action(185U, 54U) ==
           XIAOTAI_UI_ACTION_VOLUME_DOWN);
    assert(xiaotai_ui_settings_action(290U, 54U) ==
           XIAOTAI_UI_ACTION_VOLUME_UP);
    assert(xiaotai_ui_settings_action(240U, 54U) ==
           XIAOTAI_UI_ACTION_NONE);
    assert(xiaotai_ui_settings_action(50U, 54U) ==
           XIAOTAI_UI_ACTION_NONE);
    assert(xiaotai_ui_settings_action(50U, 174U) ==
           XIAOTAI_UI_ACTION_MIC_SENSITIVITY_CYCLE);
    assert(xiaotai_ui_settings_action(319U, 174U) ==
           XIAOTAI_UI_ACTION_MIC_SENSITIVITY_CYCLE);
    assert(xiaotai_ui_settings_action(50U, 158U) ==
           XIAOTAI_UI_ACTION_NONE);
    return 0;
}
