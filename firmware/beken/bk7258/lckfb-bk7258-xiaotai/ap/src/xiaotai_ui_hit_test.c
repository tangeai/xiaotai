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
    if (x < 8U || x >= 312U) return XIAOTAI_UI_ACTION_NONE;
    if (y >= 132U && y < 172U) {
        if (x < 156U) return XIAOTAI_UI_ACTION_ROOM_CREATE;
        if (x >= 164U) return XIAOTAI_UI_ACTION_ROOM_CREATE_PASSWORD;
        return XIAOTAI_UI_ACTION_NONE;
    }
    if (y >= XIAOTAI_UI_ROOM_BUTTON_Y &&
        y < XIAOTAI_UI_ROOM_BUTTON_Y + XIAOTAI_UI_ROOM_BUTTON_HEIGHT) {
        return XIAOTAI_UI_ACTION_ROOM_JOIN;
    }
    return XIAOTAI_UI_ACTION_NONE;
}

int xiaotai_ui_room_keypad_key(uint16_t x, uint16_t y)
{
    if (x >= 320U || y < 64U || y >= 180U) return -1;
    unsigned row = (unsigned)(y - 64U) / 29U;
    unsigned column = (unsigned)x / 107U;
    unsigned local_x = (unsigned)x - column * 107U;
    if (column > 2U || local_x < 4U || local_x >= 103U ||
        (unsigned)(y - 64U) % 29U >= 25U) return -1;
    if (row < 3U) return (int)(row * 3U + column + 1U);
    if (column == 0U) return XIAOTAI_UI_ROOM_KEY_DELETE;
    return column == 1U ? 0 : XIAOTAI_UI_ROOM_KEY_SUBMIT;
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
    if (x >= XIAOTAI_UI_ROOM_LEAVE_X &&
        x < XIAOTAI_UI_ROOM_LEAVE_X + XIAOTAI_UI_ROOM_BUTTON_WIDTH)
        return XIAOTAI_UI_ACTION_ROOM_LEAVE_CANCEL;
    if (x >= XIAOTAI_UI_ROOM_TALK_X &&
        x < XIAOTAI_UI_ROOM_TALK_X + XIAOTAI_UI_ROOM_BUTTON_WIDTH)
        return XIAOTAI_UI_ACTION_ROOM_LEAVE_CONFIRM;
    return XIAOTAI_UI_ACTION_NONE;
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
    if (x < 8U || x >= 312U) return XIAOTAI_UI_ACTION_NONE;
    bool down = x >= 8U && x < 50U;
    bool up = x >= 270U && x < 312U;
    if (y >= 40U && y < 78U) {
        return down ? XIAOTAI_UI_ACTION_VOLUME_DOWN :
               up ? XIAOTAI_UI_ACTION_VOLUME_UP : XIAOTAI_UI_ACTION_NONE;
    }
    if (y >= 82U && y < 120U) {
        return down ? XIAOTAI_UI_ACTION_MIC_SENSITIVITY_DOWN :
               up ? XIAOTAI_UI_ACTION_MIC_SENSITIVITY_UP : XIAOTAI_UI_ACTION_NONE;
    }
    if (y >= 124U && y < 154U) {
        return x < 156U ? XIAOTAI_UI_ACTION_SPEAKER_TOGGLE :
               x >= 164U ? XIAOTAI_UI_ACTION_MIC_TOGGLE : XIAOTAI_UI_ACTION_NONE;
    }
    if (y >= 160U && y < 190U) return XIAOTAI_UI_ACTION_SLEEP;
    if (y >= 196U && y < 226U) {
        return x < 156U ? XIAOTAI_UI_ACTION_NETWORK :
               x >= 164U ? XIAOTAI_UI_ACTION_RESET : XIAOTAI_UI_ACTION_NONE;
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
