#ifndef XIAOTAI_UI_HIT_TEST_H
#define XIAOTAI_UI_HIT_TEST_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define XIAOTAI_UI_HOME_QUICK_CALL_ICON_X 276U
#define XIAOTAI_UI_HOME_QUICK_CALL_ICON_Y 150U
#define XIAOTAI_UI_HOME_RIGHT_HIT_X 260U
#define XIAOTAI_UI_HOME_QUICK_CALL_HIT_Y 140U
#define XIAOTAI_UI_HOME_MENU_HIT_Y 198U
#define XIAOTAI_UI_INCOMING_ACTION_Y 185U
#define XIAOTAI_UI_INCOMING_ACCEPT_X 160U
#define XIAOTAI_UI_BACK_HIT_X 40U
#define XIAOTAI_UI_BACK_HIT_Y 34U
#define XIAOTAI_UI_ROOM_BUTTON_Y 184U
#define XIAOTAI_UI_ROOM_BUTTON_HEIGHT 48U
#define XIAOTAI_UI_ROOM_LEAVE_X 8U
#define XIAOTAI_UI_ROOM_TALK_X 164U
#define XIAOTAI_UI_ROOM_BUTTON_WIDTH 148U
#define XIAOTAI_UI_ROOM_PAGE_Y 158U
#define XIAOTAI_UI_ROOM_PAGE_HEIGHT 22U
#define XIAOTAI_UI_ROOM_PAGE_PREV_X 12U
#define XIAOTAI_UI_ROOM_PAGE_NEXT_X 232U
#define XIAOTAI_UI_ROOM_PAGE_BUTTON_WIDTH 76U
#define XIAOTAI_UI_ROOM_PARTICIPANTS_PER_PAGE 3U
#define XIAOTAI_UI_ROOM_KEY_DELETE (-2)
#define XIAOTAI_UI_ROOM_KEY_SUBMIT (-3)

typedef enum {
    XIAOTAI_UI_ACTION_NONE = 0,
    XIAOTAI_UI_ACTION_HOME_AI,
    XIAOTAI_UI_ACTION_HOME_QUICK_CALL,
    XIAOTAI_UI_ACTION_HOME_MENU,
    XIAOTAI_UI_ACTION_VOLUME_DOWN,
    XIAOTAI_UI_ACTION_VOLUME_UP,
    XIAOTAI_UI_ACTION_MIC_SENSITIVITY_CYCLE,
    XIAOTAI_UI_ACTION_CALL_REJECT,
    XIAOTAI_UI_ACTION_CALL_ACCEPT,
    XIAOTAI_UI_ACTION_CALL_MIC_TOGGLE,
    XIAOTAI_UI_ACTION_CALL_HANGUP,
    XIAOTAI_UI_ACTION_BACK,
    XIAOTAI_UI_ACTION_ROOM_CREATE,
    XIAOTAI_UI_ACTION_ROOM_JOIN,
    XIAOTAI_UI_ACTION_ROOM_LEAVE,
    XIAOTAI_UI_ACTION_ROOM_LEAVE_CANCEL,
    XIAOTAI_UI_ACTION_ROOM_LEAVE_CONFIRM,
    XIAOTAI_UI_ACTION_ROOM_PAGE_PREV,
    XIAOTAI_UI_ACTION_ROOM_PAGE_NEXT,
    XIAOTAI_UI_ACTION_ROOM_TALK_START,
    XIAOTAI_UI_ACTION_ROOM_TALK_STOP,
} xiaotai_ui_action_t;

xiaotai_ui_action_t xiaotai_ui_home_action(uint16_t x, uint16_t y);
xiaotai_ui_action_t xiaotai_ui_incoming_action(uint16_t x, uint16_t y);
xiaotai_ui_action_t xiaotai_ui_active_call_action(uint16_t x, uint16_t y);
xiaotai_ui_action_t xiaotai_ui_room_action(uint16_t x, uint16_t y,
                                            bool pressed);
xiaotai_ui_action_t xiaotai_ui_room_entry_action(uint16_t x, uint16_t y);
int xiaotai_ui_room_keypad_key(uint16_t x, uint16_t y);
xiaotai_ui_action_t xiaotai_ui_room_leave_confirm_action(uint16_t x,
                                                          uint16_t y);
size_t xiaotai_ui_room_page_count(size_t participant_count);
size_t xiaotai_ui_room_page_clamp(size_t page, size_t participant_count);
xiaotai_ui_action_t xiaotai_ui_settings_action(uint16_t x, uint16_t y);
/** Actions that must not depend on a later TOUCH_UP from the controller. */
bool xiaotai_ui_action_triggers_on_down(xiaotai_ui_action_t action);

#endif
