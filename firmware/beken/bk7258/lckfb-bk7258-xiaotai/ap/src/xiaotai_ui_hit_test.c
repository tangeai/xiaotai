#include "xiaotai_ui_hit_test.h"

xiaotai_ui_action_t xiaotai_ui_home_action(uint16_t x, uint16_t y)
{
    if (x >= XIAOTAI_UI_HOME_RIGHT_HIT_X &&
        y >= XIAOTAI_UI_HOME_MENU_HIT_Y) {
        return XIAOTAI_UI_ACTION_HOME_MENU;
    }
    if (x >= XIAOTAI_UI_HOME_RIGHT_HIT_X &&
        y >= XIAOTAI_UI_HOME_QUICK_CALL_HIT_Y) {
        return XIAOTAI_UI_ACTION_HOME_QUICK_CALL;
    }
    return XIAOTAI_UI_ACTION_HOME_AI;
}

xiaotai_ui_action_t xiaotai_ui_incoming_action(uint16_t x, uint16_t y)
{
    if (y < XIAOTAI_UI_INCOMING_ACTION_Y) {
        return XIAOTAI_UI_ACTION_NONE;
    }
    return x < XIAOTAI_UI_INCOMING_ACCEPT_X ?
           XIAOTAI_UI_ACTION_CALL_REJECT : XIAOTAI_UI_ACTION_CALL_ACCEPT;
}

xiaotai_ui_action_t xiaotai_ui_active_call_action(uint16_t x, uint16_t y)
{
    if (y < XIAOTAI_UI_INCOMING_ACTION_Y) {
        return XIAOTAI_UI_ACTION_NONE;
    }
    return x < XIAOTAI_UI_INCOMING_ACCEPT_X ?
           XIAOTAI_UI_ACTION_CALL_MIC_TOGGLE :
           XIAOTAI_UI_ACTION_CALL_HANGUP;
}

xiaotai_ui_action_t xiaotai_ui_room_action(uint16_t x, uint16_t y,
                                            bool pressed)
{
    if (pressed && x < XIAOTAI_UI_BACK_HIT_X &&
        y < XIAOTAI_UI_BACK_HIT_Y) {
        return XIAOTAI_UI_ACTION_BACK;
    }
    if (y >= XIAOTAI_UI_ROOM_PAGE_Y &&
        y < XIAOTAI_UI_ROOM_PAGE_Y + XIAOTAI_UI_ROOM_PAGE_HEIGHT) {
        if (x >= XIAOTAI_UI_ROOM_PAGE_PREV_X &&
            x < XIAOTAI_UI_ROOM_PAGE_PREV_X +
                XIAOTAI_UI_ROOM_PAGE_BUTTON_WIDTH) {
            return pressed ? XIAOTAI_UI_ACTION_NONE :
                             XIAOTAI_UI_ACTION_ROOM_PAGE_PREV;
        }
        if (x >= XIAOTAI_UI_ROOM_PAGE_NEXT_X &&
            x < XIAOTAI_UI_ROOM_PAGE_NEXT_X +
                XIAOTAI_UI_ROOM_PAGE_BUTTON_WIDTH) {
            return pressed ? XIAOTAI_UI_ACTION_NONE :
                             XIAOTAI_UI_ACTION_ROOM_PAGE_NEXT;
        }
        return XIAOTAI_UI_ACTION_NONE;
    }
    if (y < XIAOTAI_UI_ROOM_BUTTON_Y ||
        y >= XIAOTAI_UI_ROOM_BUTTON_Y + XIAOTAI_UI_ROOM_BUTTON_HEIGHT) {
        return XIAOTAI_UI_ACTION_NONE;
    }
    if (x >= XIAOTAI_UI_ROOM_LEAVE_X &&
        x < XIAOTAI_UI_ROOM_LEAVE_X + XIAOTAI_UI_ROOM_BUTTON_WIDTH) {
        return pressed ? XIAOTAI_UI_ACTION_ROOM_LEAVE :
                         XIAOTAI_UI_ACTION_NONE;
    }
    if (x >= XIAOTAI_UI_ROOM_TALK_X &&
        x < XIAOTAI_UI_ROOM_TALK_X + XIAOTAI_UI_ROOM_BUTTON_WIDTH) {
        return pressed ? XIAOTAI_UI_ACTION_ROOM_TALK_START :
                         XIAOTAI_UI_ACTION_ROOM_TALK_STOP;
    }
    return XIAOTAI_UI_ACTION_NONE;
}

xiaotai_ui_action_t xiaotai_ui_room_entry_action(uint16_t x, uint16_t y)
{
    if (x < XIAOTAI_UI_BACK_HIT_X && y < XIAOTAI_UI_BACK_HIT_Y) {
        return XIAOTAI_UI_ACTION_BACK;
    }
    if (y < XIAOTAI_UI_ROOM_BUTTON_Y ||
        y >= XIAOTAI_UI_ROOM_BUTTON_Y + XIAOTAI_UI_ROOM_BUTTON_HEIGHT) {
        return XIAOTAI_UI_ACTION_NONE;
    }
    return x < 160U ? XIAOTAI_UI_ACTION_ROOM_CREATE :
                      XIAOTAI_UI_ACTION_ROOM_JOIN;
}

int xiaotai_ui_room_keypad_key(uint16_t x, uint16_t y)
{
    if (y >= 190U && y < 232U) return XIAOTAI_UI_ROOM_KEY_SUBMIT;
    if (y < 64U || y >= 180U) return -1;
    unsigned row = (unsigned)(y - 64U) / 29U;
    unsigned column = (unsigned)x / 107U;
    if (column > 2U) column = 2U;
    if (row < 3U) return (int)(row * 3U + column + 1U);
    if (column == 0U) return XIAOTAI_UI_ROOM_KEY_DELETE;
    return column == 1U ? 0 : -1;
}

xiaotai_ui_action_t xiaotai_ui_room_leave_confirm_action(uint16_t x,
                                                          uint16_t y)
{
    if (x < XIAOTAI_UI_BACK_HIT_X && y < XIAOTAI_UI_BACK_HIT_Y) {
        return XIAOTAI_UI_ACTION_ROOM_LEAVE_CANCEL;
    }
    if (y < XIAOTAI_UI_ROOM_BUTTON_Y ||
        y >= XIAOTAI_UI_ROOM_BUTTON_Y + XIAOTAI_UI_ROOM_BUTTON_HEIGHT) {
        return XIAOTAI_UI_ACTION_NONE;
    }
    return x < 160U ? XIAOTAI_UI_ACTION_ROOM_LEAVE_CANCEL :
                      XIAOTAI_UI_ACTION_ROOM_LEAVE_CONFIRM;
}

size_t xiaotai_ui_room_page_count(size_t participant_count)
{
    if (participant_count == 0U) return 1U;
    return (participant_count + XIAOTAI_UI_ROOM_PARTICIPANTS_PER_PAGE - 1U) /
           XIAOTAI_UI_ROOM_PARTICIPANTS_PER_PAGE;
}

size_t xiaotai_ui_room_page_clamp(size_t page, size_t participant_count)
{
    size_t pages = xiaotai_ui_room_page_count(participant_count);
    return page < pages ? page : pages - 1U;
}

xiaotai_ui_action_t xiaotai_ui_settings_action(uint16_t x, uint16_t y)
{
    if (y >= 160U && y < 196U) {
        return XIAOTAI_UI_ACTION_MIC_SENSITIVITY_CYCLE;
    }
    if (y < 42U || y >= 88U) {
        return XIAOTAI_UI_ACTION_NONE;
    }
    if (x >= 160U && x < 215U) {
        return XIAOTAI_UI_ACTION_VOLUME_DOWN;
    }
    if (x >= 265U) {
        return XIAOTAI_UI_ACTION_VOLUME_UP;
    }
    return XIAOTAI_UI_ACTION_NONE;
}

bool xiaotai_ui_action_triggers_on_down(xiaotai_ui_action_t action)
{
    return action == XIAOTAI_UI_ACTION_HOME_QUICK_CALL ||
           action == XIAOTAI_UI_ACTION_BACK ||
           action == XIAOTAI_UI_ACTION_ROOM_LEAVE ||
           action == XIAOTAI_UI_ACTION_ROOM_TALK_START;
}
