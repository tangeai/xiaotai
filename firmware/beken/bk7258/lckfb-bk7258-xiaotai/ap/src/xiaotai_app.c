#include "xiaotai_app.h"

#include <common/bk_err.h>
#include "xiaotai_log.h"
#include <components/system.h>
#include <driver/trng.h>
#include <os/mem.h>
#include <os/os.h>
#include <modules/wifi.h>
#include <stdint.h>
#include <stdatomic.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

#include "cJSON.h"

#include "xiaotai_runtime.h"
#include "xiaotai_ai_protocol.h"
#include "xiaotai_ai_view.h"
#include "xiaotai_ai_feedback.h"
#include "xiaotai_audio.h"
#include "xiaotai_audio_policy.h"
#include "board_touch.h"
#include "xiaotai_button.h"
#include "xiaotai_call_protocol.h"
#include "xiaotai_call_state.h"
#include "xiaotai_contacts.h"
#include "xiaotai_intent.h"
#include "xiaotai_tirtc.h"
#include "xiaotai_tirtc_recovery.h"
#include "xiaotai_time.h"
#include "xiaotai_network.h"
#include "xiaotai_metrics.h"
#include "xiaotai_platform_client.h"
#include "xiaotai_room.h"
#include "xiaotai_signal.h"
#include "xiaotai_storage.h"
#include "xiaotai_touch.h"
#include "xiaotai_ui.h"
#include "xiaotai_ui_hit_test.h"

#define TAG "xiaotai_app"
#define CONTROL_QUEUE_DEPTH 10U
#define CONTROL_POLL_MS 250U
#define SESSION_CONNECT_TIMEOUT_MS 15000U
#define SESSION_INCOMING_TIMEOUT_MS 45000U
#define SESSION_OUTGOING_TIMEOUT_MS 30000U
#define SESSION_END_TIMEOUT_MS 2000U
#define STREAM_MEDIA_DIAGNOSTIC_MS 3000U
#define AI_END_PLAYBACK_QUIET_MS 120U
#define AI_END_FINAL_AUDIO_ARRIVAL_MS 1500U
#define AI_END_DRAIN_TIMEOUT_MS 5000U
#define CONTROL_TASK_STACK_SIZE (12U * 1024U)
#define ROOM_SERVICE_TASK_STACK_SIZE (12U * 1024U)
#define ROOM_TRANSPORT_RELEASE_TIMEOUT_MS 3000U
#define TIRTC_SUPERVISOR_STACK_SIZE (32U * 1024U)

typedef enum {
    CONTROL_BUTTON = 1,
    CONTROL_AI_TOKEN,
    CONTROL_AI_CONNECTED,
    CONTROL_PLATFORM_SIGNAL,
    CONTROL_CALL_INFO,
    CONTROL_CONTACTS,
    CONTROL_AI_CALL_INTENT,
    CONTROL_AI_UI,
    CONTROL_AI_END,
    CONTROL_OUTBOUND_CALL,
    CONTROL_DIAL_CONTACT,
    CONTROL_TOUCH,
    CONTROL_ROOM_ASSIGNMENT,
    CONTROL_ROOM_TOKEN,
    CONTROL_ROOM_PRESENCE,
    CONTROL_ROOM_CONNECTED,
    CONTROL_ROOM_COMMAND,
    CONTROL_ROOM_DISCONNECTED,
    CONTROL_CONSOLE_CALL,
    CONTROL_CONSOLE_CALL_ACTION,
    CONTROL_CONSOLE_ROOM,
    CONTROL_ACCEPT_PENDING,
} control_event_type_t;

typedef struct {
    control_event_type_t type;
    uint32_t generation;
    uint16_t x;
    uint16_t y;
    uint8_t input_event;
    int error;
    char *text;
} control_event_t;

typedef enum {
    UI_PAGE_HOME = 0,
    UI_PAGE_LAUNCHER,
    UI_PAGE_CONTACTS,
    UI_PAGE_CONTACT_DETAIL,
    UI_PAGE_ROOM,
    UI_PAGE_ROOM_JOIN_CODE,
    UI_PAGE_ROOM_PASSWORD,
    UI_PAGE_ROOM_LEAVE_CONFIRM,
    UI_PAGE_EXPRESSIONS,
    UI_PAGE_SETTINGS,
    UI_PAGE_NETWORK,
    UI_PAGE_DIAGNOSTICS,
    UI_PAGE_RESET_CONFIRM,
    UI_PAGE_WECHAT_QR,
} ui_page_t;

static xiaotai_runtime_t s_runtime;
static beken_mutex_t s_runtime_mutex;
static beken_mutex_t s_room_snapshot_mutex;
static beken_queue_t s_control_queue;
static beken_thread_t s_control_thread;
static uint32_t s_stream_transport_generation;
static uint32_t s_stream_runtime_generation;
static uint32_t s_stream_media_diagnostic_due_ms;
static char s_device_id[65];
static char s_ai_peer_id[512];
static char s_ai_token[1024];
static char s_ai_role_id[65];
static char s_ai_request_id[24];
static xiaotai_call_state_t s_calls;
static char *s_voip_reject_request;
static xiaotai_contacts_t s_contacts;
static xiaotai_contact_t s_pending_contact;
static uint32_t s_pending_dial_generation;
static xiaotai_ai_view_t s_ai_view;
static xiaotai_ai_end_drain_t s_ai_end_drain;
static uint32_t s_home_last_refresh_ms;
static uint32_t s_home_defer_until_ms;
static ui_page_t s_ui_page = UI_PAGE_HOME;
static unsigned s_reset_status;
static unsigned s_diagnostics_tab;
static uint32_t s_diagnostics_due_ms;
static size_t s_contact_page;
static size_t s_contact_selected;
static size_t s_contact_display_indices[XIAOTAI_CONTACTS_MAX];
static size_t s_contact_display_count;
static unsigned s_launcher_selected = 6U;
static unsigned s_expression_selected;
static bool s_clock_face;
static bool s_qr_waiting_for_wechat;
static xiaotai_product_settings_t s_settings;
static bool s_call_microphone_muted;
static uint32_t s_last_activity_ms;
static xiaotai_tirtc_recovery_t s_tirtc_recovery;
static xiaotai_tirtc_config_t s_tirtc_config;
static char s_tirtc_device_id[65];
static char s_tirtc_device_secret[257];
static char s_tirtc_client_id[65];
static char s_tirtc_endpoint[256];
static bool s_tirtc_config_ready;

static xiaotai_room_t s_room;
static xiaotai_room_snapshot_t s_room_snapshot;
static size_t s_room_participant_page;
static char s_room_code_input[7];
static size_t s_room_code_input_length;
static char s_room_password_input[5];
static size_t s_room_password_input_length;
static bool s_room_input_create;
static bool s_room_key_talking;
static bool s_room_touch_talking;
static uint32_t s_room_touch_generation;
/* Touch producer publishes terminal edges independently of the bounded
 * control queue. Only the control task changes room/PTT business state. */
static atomic_uint s_touch_release_generation;
static bool s_touch_wake_consumed;
static bool s_touch_action_consumed;
static control_event_t s_after_room_event;
static bool s_after_room_pending;
static bool s_after_room_transport_closed;
static uint32_t s_after_room_closed_ms;
static bool s_accept_pending_after_media;

#define TIRTC_TRANSPORT_QUIET_MS 500U

static const uint32_t s_screen_timeout_ms[] = {
    60000U, 300000U, 600000U, 1800000U, 0U,
};
static const char *const s_screen_timeout_names[] = {
    "1分钟", "5分钟", "10分钟", "30分钟", "从不",
};

static void finish_call(uint32_t generation, int error);
static void finish_voip(uint32_t generation, int error);
static void clear_voip_fields(void);
static void reject_device_call(const char *room_id, const char *reason);
static void reject_voip_request(char *request);
static void dial_contact(const xiaotai_contact_t *contact);
static void quick_call_first_contact(void);
static void quick_call_first_wechat_contact(void);
static void room_render(void);
static void handle_ai_connected(uint32_t generation);
static void request_accept_pending(void);
static void request_reject_pending(void);
static void accept_pending_call(void);

static void queue_accept_pending(void)
{
    control_event_t event = {.type = CONTROL_ACCEPT_PENDING};
    if (rtos_push_to_queue(&s_control_queue, &event, 0) != BK_OK) {
        BK_LOGE(TAG, "pending call acceptance event dropped\n");
    }
}

static bool show_pending_incoming(void)
{
    rtos_lock_mutex(&s_runtime_mutex);
    xiaotai_session_owner_t pending = s_runtime.pending_incoming;
    rtos_unlock_mutex(&s_runtime_mutex);
    if (pending == XIAOTAI_OWNER_DEVICE_CALL) {
        xiaotai_ui_show_status("DEVICE CALL");
        return true;
    }
    if (pending == XIAOTAI_OWNER_WECHAT_VOIP) {
        xiaotai_ui_show_status("VOIP CALL");
        return true;
    }
    return false;
}

static void arm_runtime_timeout_locked(uint32_t generation,
                                       uint32_t timeout_ms)
{
    (void)xiaotai_runtime_arm_timeout(&s_runtime, generation,
                                      rtos_get_time(), timeout_ms);
}

static void queue_text_event(control_event_type_t type, uint32_t generation,
                             const char *text, size_t length)
{
    control_event_t event = {.type = type, .generation = generation};
    if (text != NULL && length > 0U) {
        event.text = os_malloc(length + 1U);
        if (event.text != NULL) {
            memcpy(event.text, text, length);
            event.text[length] = '\0';
        }
    }
    if ((text != NULL && length > 0U && event.text == NULL) ||
        s_control_queue == NULL ||
        rtos_push_to_queue(&s_control_queue, &event, 0) != BK_OK) {
        os_free(event.text);
        BK_LOGW(TAG, "control event dropped type=%d\n", (int)type);
    }
}

static void finish_ai(uint32_t generation, int error)
{
    rtos_lock_mutex(&s_runtime_mutex);
    bool media_was_active = s_runtime.owner == XIAOTAI_OWNER_AI &&
                            s_runtime.generation == generation &&
                            s_runtime.state == XIAOTAI_STATE_AI_ACTIVE;
    bool expected_remote_close = error == TIRTC_E_CONN_REMOTECLOSE &&
        xiaotai_ai_remote_close_is_normal(&s_ai_end_drain, generation,
                                           media_was_active);
    xiaotai_ai_end_drain_cancel(&s_ai_end_drain);
    bool finished = s_runtime.owner == XIAOTAI_OWNER_AI &&
                    xiaotai_runtime_finish(&s_runtime, generation);
    rtos_unlock_mutex(&s_runtime_mutex);
    if (finished) {
        if (expected_remote_close) {
            BK_LOGI(TAG,
                    "AI active session completed by remote close generation=%u\n",
                    (unsigned)generation);
            error = 0;
        }
        xiaotai_ai_view_init(&s_ai_view);
        s_ui_page = UI_PAGE_HOME;
        if (error != 0) s_home_defer_until_ms = rtos_get_time() + 5000U;
        if (!show_pending_incoming()) {
            xiaotai_ui_show_status(error == 0 ? "READY" : "ERROR");
        }
        BK_LOGI(TAG, "AI session finished generation=%u error=%d\n",
                (unsigned)generation, error);
    }
}

static void stop_ai(uint32_t generation)
{
    if (xiaotai_tirtc_connected()) {
        static const char end[] =
            "{\"jsonrpc\":\"2.0\",\"method\":\"end_session\"}";
        (void)xiaotai_tirtc_send_command(XIAOTAI_AI_COMMAND, end,
                                         sizeof(end) - 1U);
        (void)xiaotai_tirtc_disconnect();
        xiaotai_ui_show_status("ENDING");
    } else {
        (void)xiaotai_tirtc_disconnect(); /* Also cancels a pending WHIP result. */
        finish_ai(generation, 0);
    }
}

static void ai_token_response(const char *body, void *context)
{
    queue_text_event(CONTROL_AI_TOKEN, (uint32_t)(uintptr_t)context,
                     body, body == NULL ? 0U : strlen(body));
}

static void call_info_response(const char *body, void *context)
{
    queue_text_event(CONTROL_CALL_INFO, (uint32_t)(uintptr_t)context,
                     body, body == NULL ? 0U : strlen(body));
}

static void contacts_response(const char *body, void *context)
{
    (void)context;
    queue_text_event(CONTROL_CONTACTS, 0, body,
                     body == NULL ? 0U : strlen(body));
}

static void outbound_call_response(const char *body, void *context)
{
    queue_text_event(CONTROL_OUTBOUND_CALL,
                     (uint32_t)(uintptr_t)context, body,
                     body == NULL ? 0U : strlen(body));
}

static void refresh_contacts(void)
{
    int rc = xiaotai_platform_service_request("/v1/call/device/contacts",
                                               NULL, contacts_response, NULL);
    if (rc != 0) {
        BK_LOGW(TAG, "combined contact refresh failed rc=%d\n", rc);
    }
}

static void show_wechat_qr_waiting(bool wechat_only)
{
    s_qr_waiting_for_wechat = wechat_only;
    s_ui_page = UI_PAGE_WECHAT_QR;
    xiaotai_ui_show_wechat_qr();
}

static void quick_call_first_contact(void)
{
    xiaotai_contact_t contact = {0};
    xiaotai_contact_match_t match =
        xiaotai_contacts_first(&s_contacts, &contact);
    if (match != XIAOTAI_CONTACT_MATCH_OK) {
        show_wechat_qr_waiting(false);
        BK_LOGW(TAG, "quick call: platform contact list is empty\n");
        return;
    }
    BK_LOGI(TAG, "quick call dialing contacts[0] name=%s type=%d\n",
            contact.name, (int)contact.type);
    dial_contact(&contact);
}

static void quick_call_first_wechat_contact(void)
{
    xiaotai_contact_t contact = {0};
    xiaotai_contact_match_t match = xiaotai_contacts_first_of_type(
        &s_contacts, XIAOTAI_CONTACT_VOIP, &contact);
    if (match != XIAOTAI_CONTACT_MATCH_OK) {
        show_wechat_qr_waiting(true);
        BK_LOGW(TAG, "home call: no WeChat contact; showing bind QR\n");
        return;
    }
    BK_LOGI(TAG, "home call dialing first WeChat contact name=%s\n",
            contact.name);
    dial_contact(&contact);
}

static void service_response_ignored(const char *body, void *context)
{
    (void)body;
    (void)context;
}

static void input_activity(void)
{
    s_last_activity_ms = rtos_get_time();
    if (!xiaotai_ui_backlight_on()) (void)xiaotai_ui_set_backlight(true);
}

static int apply_microphone_sensitivity(void *context, unsigned sensitivity)
{
    (void)context;
    return xiaotai_audio_set_microphone_sensitivity(sensitivity);
}

static void button_pressed(xiaotai_button_event_t button_event, void *context)
{
    (void)context;
    static const xiaotai_intent_t intents[] = {
        [XIAOTAI_BUTTON_SHORT] = XIAOTAI_INTENT_PRIMARY,
        [XIAOTAI_BUTTON_DOUBLE] = XIAOTAI_INTENT_QUICK_CALL,
        [XIAOTAI_BUTTON_LONG] = XIAOTAI_INTENT_PTT_HOLD,
        [XIAOTAI_BUTTON_DOWN] = XIAOTAI_INTENT_PTT_PRESS,
        [XIAOTAI_BUTTON_UP] = XIAOTAI_INTENT_PTT_RELEASE,
    };
    xiaotai_intent_t intent = button_event < sizeof(intents) / sizeof(intents[0]) ?
        intents[button_event] : XIAOTAI_INTENT_NONE;
    if (intent == XIAOTAI_INTENT_NONE) return;
    control_event_t event = {
        .type = CONTROL_BUTTON,
        .input_event = (uint8_t)intent,
    };
    if (rtos_push_to_queue(&s_control_queue, &event, 0) != BK_OK) {
        BK_LOGW(TAG, "AI button event dropped\n");
    }
}

static void touch_event(xiaotai_touch_event_t touch, uint16_t x, uint16_t y,
                        void *context)
{
    (void)context;
    static uint16_t start_x;
    static uint16_t start_y;
    static bool tracking;
    static uint32_t gesture_generation;
    if (touch == XIAOTAI_TOUCH_DOWN) {
        if (++gesture_generation == 0U) ++gesture_generation;
        start_x = x;
        start_y = y;
        tracking = true;
        control_event_t event = {
            .type = CONTROL_TOUCH,
            .generation = gesture_generation,
            .x = x,
            .y = y,
            .input_event = (uint8_t)touch,
        };
        if (rtos_push_to_queue(&s_control_queue, &event, 0) != BK_OK) {
            BK_LOGW(TAG, "touch event dropped\n");
        }
        return;
    }
    if (touch != XIAOTAI_TOUCH_UP && touch != XIAOTAI_TOUCH_CANCEL) return;
    control_event_t event = {
        .type = CONTROL_TOUCH,
        .generation = gesture_generation,
        .x = x,
        .y = y,
        .input_event = (uint8_t)touch,
    };
    atomic_store(&s_touch_release_generation, gesture_generation);
    BK_LOGD(TAG, "touch release generation=%u cancelled=%u\n",
            (unsigned)gesture_generation, touch == XIAOTAI_TOUCH_CANCEL ? 1U : 0U);
    if (touch == XIAOTAI_TOUCH_CANCEL) {
        tracking = false;
        if (rtos_push_to_queue(&s_control_queue, &event, 0) != BK_OK) {
            BK_LOGW(TAG, "touch cancel event dropped\n");
        }
        return;
    }
    if (tracking) {
        int dx = (int)x - (int)start_x;
        int dy = (int)y - (int)start_y;
        int abs_dx = dx < 0 ? -dx : dx;
        int abs_dy = dy < 0 ? -dy : dy;
        if (abs_dx >= 50 && abs_dx > abs_dy) {
            event.input_event = XIAOTAI_TOUCH_MOVE;
            event.error = dx < 0 ? -1 : 1;
        }
    }
    tracking = false;
    if (rtos_push_to_queue(&s_control_queue, &event, 0) != BK_OK) {
        BK_LOGW(TAG, "touch event dropped\n");
    }
}

static void render_contacts(void)
{
    const char *names[XIAOTAI_CONTACTS_MAX] = {0};
    bool online[XIAOTAI_CONTACTS_MAX] = {0};
    bool wechat[XIAOTAI_CONTACTS_MAX] = {0};
    size_t selected_display = 0U;
    s_contact_display_count = xiaotai_contacts_grouped_indices(
        &s_contacts, s_contact_display_indices, XIAOTAI_CONTACTS_MAX);
    for (size_t i = 0; i < s_contact_display_count; ++i) {
        size_t source = s_contact_display_indices[i];
        names[i] = s_contacts.entries[source].name;
        online[i] = s_contacts.entries[source].online;
        wechat[i] = s_contacts.entries[source].type == XIAOTAI_CONTACT_VOIP;
        if (source == s_contact_selected) selected_display = i;
    }
    xiaotai_ui_show_contacts(names, online, wechat, s_contact_display_count,
                             s_contact_page, selected_display);
}

static void render_settings(void)
{
    uint8_t index = s_settings.screen_timeout_index;
    if (index >= sizeof(s_screen_timeout_names) /
                 sizeof(s_screen_timeout_names[0])) index = 1U;
    xiaotai_ui_show_settings(s_settings.volume, s_settings.speaker_muted,
                             s_settings.microphone_muted,
                             s_settings.microphone_sensitivity,
                             s_screen_timeout_names[index]);
}

static void render_contact_detail(void)
{
    if (s_contact_selected >= s_contacts.count) {
        s_ui_page = UI_PAGE_CONTACTS;
        render_contacts();
        return;
    }
    const xiaotai_contact_t *contact = &s_contacts.entries[s_contact_selected];
    xiaotai_ui_show_contact_detail(contact->name, contact->online,
                                   contact->type == XIAOTAI_CONTACT_VOIP);
}

static void render_expressions(void)
{
    xiaotai_ui_show_expressions(s_expression_selected, s_ai_view.emotion);
}

static void render_network(void)
{
    wifi_link_status_t link = {0};
    int rssi = bk_wifi_sta_get_link_status(&link) == BK_OK ?
               link.rssi : -128;
    char ip[20] = {0};
    (void)xiaotai_network_get_ip(ip, sizeof(ip));
    xiaotai_ui_show_network(rssi, ip[0] == '\0' ? NULL : ip);
}

static void render_diagnostics(void)
{
    char details[768];
    if (s_diagnostics_tab == 0U) {
        xiaotai_metrics_snapshot_t snapshot;
        xiaotai_metrics_snapshot(&snapshot);
        char cpu[24];
        if (snapshot.sampled) snprintf(cpu, sizeof(cpu), "%u%%", snapshot.cpu_percent);
        else snprintf(cpu, sizeof(cpu), "等待采样");
        snprintf(details, sizeof(details),
            "运行 %u s  状态 %s\nCPU %s  任务 %u\n"
            "内存 KiB：可用 / 最低\nRAM %u / %u\nPSRAM %u / %u\n"
            "最低栈剩余 %u B\n任务 %s\n最大连续块：未提供",
            (unsigned)(rtos_get_time() / 1000U), snapshot.state, cpu, snapshot.task_count,
            (unsigned)(rtos_get_free_heap_size() / 1024U),
            (unsigned)(rtos_get_minimum_free_heap_size() / 1024U),
            (unsigned)(rtos_get_psram_free_heap_size() / 1024U),
            (unsigned)(rtos_get_psram_minimum_free_heap_size() / 1024U),
            snapshot.stack_low_words * (unsigned)sizeof(uint32_t), snapshot.stack_low_task);
    } else if (s_diagnostics_tab == 1U) {
        snprintf(details, sizeof(details),
            "音频 %s\n麦克风 %s\n扬声器 %s\n扬声器音量 %u / 10\n"
            "麦克风灵敏度 %u / 5\n上行 %s",
            xiaotai_audio_running() ? "运行中" : "空闲",
            s_settings.microphone_muted ? "已关闭" : "已开启",
            s_settings.speaker_muted ? "已关闭" : "已开启",
            s_settings.volume, s_settings.microphone_sensitivity,
            xiaotai_audio_running() && xiaotai_audio_uplink_enabled() ? "已开启" : "已关闭");
    } else {
        xiaotai_metrics_copy_events(details, sizeof(details));
    }
    xiaotai_ui_show_diagnostics(s_diagnostics_tab, details);
    s_diagnostics_due_ms = rtos_get_time() + 1000U;
}

static bool room_menu_page(ui_page_t page)
{
    return page == UI_PAGE_ROOM || page == UI_PAGE_ROOM_JOIN_CODE ||
           page == UI_PAGE_ROOM_PASSWORD ||
           page == UI_PAGE_ROOM_LEAVE_CONFIRM;
}

static void navigate_to(ui_page_t page)
{
    if (s_ui_page == page) return;
    if (room_menu_page(s_ui_page) && !room_menu_page(page)) {
        s_room_key_talking = false;
        s_room_touch_talking = false;
        int rc = xiaotai_room_action(&s_room, XIAOTAI_ROOM_ACTION_END, NULL);
        BK_LOGI(TAG, "room menu exited target=%d rc=%d\n", (int)page, rc);
    }
    s_ui_page = page;
}

/* Called only by the state-owning control task. 0=open, 1=cancel, 2=confirm. */
static void handle_reset_action(unsigned action)
{
    if (s_reset_status == 1U) return;
    if (action == 0U) {
        s_reset_status = 0U;
        navigate_to(UI_PAGE_RESET_CONFIRM);
        xiaotai_ui_show_reset_confirmation(s_reset_status);
    } else if (action == 1U) {
        navigate_to(UI_PAGE_SETTINGS);
        render_settings();
    } else if (action == 2U && s_ui_page == UI_PAGE_RESET_CONFIRM) {
        s_reset_status = 1U;
        xiaotai_ui_show_reset_confirmation(s_reset_status);
        if (xiaotai_storage_reset_user_data() != BK_OK) {
            s_reset_status = 2U;
            xiaotai_ui_show_reset_confirmation(s_reset_status);
            return;
        }
        rtos_delay_milliseconds(300U);
        bk_reboot();
    }
}

static void return_home_from_room(void)
{
    navigate_to(UI_PAGE_HOME);
    s_home_last_refresh_ms = 0U;
    s_home_defer_until_ms = rtos_get_time() + 2000U;
    xiaotai_ui_show_status("ROOM PAUSED");
}

static bool open_launcher_item(unsigned item)
{
    switch (item % 6U) {
    case 0:
        navigate_to(UI_PAGE_HOME);
        return true;
    case 1:
        navigate_to(UI_PAGE_CONTACTS);
        s_contact_page = 0U;
        s_contact_selected = 0U;
        render_contacts();
        break;
    case 2:
        navigate_to(UI_PAGE_ROOM);
        s_room_participant_page = 0U;
        (void)xiaotai_room_action(&s_room, XIAOTAI_ROOM_ACTION_SYNC, NULL);
        room_render();
        break;
    case 3:
        navigate_to(UI_PAGE_EXPRESSIONS);
        render_expressions();
        break;
    case 4:
        navigate_to(UI_PAGE_SETTINGS);
        render_settings();
        break;
    default:
        navigate_to(UI_PAGE_DIAGNOSTICS);
        s_diagnostics_tab = 0U;
        render_diagnostics();
        break;
    }
    return false;
}

static void accept_pending_call(void)
{
    xiaotai_session_owner_t owner = XIAOTAI_OWNER_NONE;
    uint32_t generation = 0U;
    char room_id[sizeof(s_calls.device.room_id)];
    char peer_id[sizeof(s_calls.device.peer_id)];

    rtos_lock_mutex(&s_runtime_mutex);
    bool accepted = xiaotai_runtime_accept_incoming(&s_runtime, &owner,
                                                     &generation);
    if (accepted) {
        (void)xiaotai_runtime_connected(&s_runtime, generation);
        arm_runtime_timeout_locked(generation, SESSION_CONNECT_TIMEOUT_MS);
    }
    snprintf(room_id, sizeof(room_id), "%s",
             owner == XIAOTAI_OWNER_DEVICE_CALL ? s_calls.device.room_id :
                                                  s_calls.voip.room_id);
    snprintf(peer_id, sizeof(peer_id), "%s", s_calls.device.peer_id);
    if (accepted && owner == XIAOTAI_OWNER_WECHAT_VOIP) {
        os_free(s_voip_reject_request);
        s_voip_reject_request = NULL;
    }
    rtos_unlock_mutex(&s_runtime_mutex);
    if (!accepted) return;

    if (owner == XIAOTAI_OWNER_DEVICE_CALL) {
        char request[320];
        int length = xiaotai_call_encode_device_info(
            request, sizeof(request), peer_id, room_id);
        int rc = length > 0 && (size_t)length < sizeof(request) ?
            xiaotai_platform_service_request("/v1/call/device/info",
                request, call_info_response,
                (void *)(uintptr_t)generation) : BK_FAIL;
        if (rc != 0) finish_call(generation, rc);
        else xiaotai_ui_show_status("DEVICE CONNECT");
    } else {
        int rc = xiaotai_tirtc_voip_connect(s_calls.voip.peer_id, s_calls.voip.token,
                                             generation);
        if (rc != 0) finish_voip(generation, rc);
        else xiaotai_ui_show_status("VOIP CONNECT");
    }
    BK_LOGI(TAG, "accepted pending call owner=%d room=%s generation=%u\n",
            (int)owner, room_id, (unsigned)generation);
}

static void request_accept_pending(void)
{
    if (xiaotai_room_active(&s_room)) {
        if (s_after_room_pending) os_free(s_after_room_event.text);
        s_after_room_event = (control_event_t){
            .type = CONTROL_ACCEPT_PENDING,
        };
        s_after_room_pending = true;
        s_after_room_transport_closed = false;
        s_after_room_closed_ms = 0U;
        xiaotai_room_disconnect(&s_room, "suspended");
        return;
    }

    rtos_lock_mutex(&s_runtime_mutex);
    xiaotai_session_owner_t owner = s_runtime.owner;
    bool waiting = xiaotai_runtime_has_incoming(&s_runtime);
    if (waiting && (owner == XIAOTAI_OWNER_AI ||
                    owner == XIAOTAI_OWNER_STREAM)) {
        s_accept_pending_after_media = true;
    }
    uint32_t generation = s_runtime.generation;
    rtos_unlock_mutex(&s_runtime_mutex);
    if (!waiting) return;
    if (owner == XIAOTAI_OWNER_AI) {
        stop_ai(generation);
        rtos_lock_mutex(&s_runtime_mutex);
        bool released = s_runtime.owner == XIAOTAI_OWNER_NONE &&
                        s_accept_pending_after_media;
        if (released) s_accept_pending_after_media = false;
        rtos_unlock_mutex(&s_runtime_mutex);
        if (released) queue_accept_pending();
    } else if (owner == XIAOTAI_OWNER_STREAM) {
        (void)xiaotai_tirtc_disconnect();
    } else if (owner == XIAOTAI_OWNER_NONE) {
        accept_pending_call();
    }
}

static void request_reject_pending(void)
{
    char room_id[sizeof(s_calls.device.room_id)] = {0};
    char *voip_request = NULL;
    xiaotai_session_owner_t pending = XIAOTAI_OWNER_NONE;
    xiaotai_session_owner_t active = XIAOTAI_OWNER_NONE;

    rtos_lock_mutex(&s_runtime_mutex);
    pending = s_runtime.pending_incoming;
    active = s_runtime.owner;
    if (pending == XIAOTAI_OWNER_DEVICE_CALL) {
        snprintf(room_id, sizeof(room_id), "%s", s_calls.device.room_id);
        xiaotai_call_state_clear_device(&s_calls);
    } else if (pending == XIAOTAI_OWNER_WECHAT_VOIP) {
        voip_request = s_voip_reject_request;
        s_voip_reject_request = NULL;
        clear_voip_fields();
    }
    bool cancelled = xiaotai_runtime_cancel_incoming(&s_runtime, NULL);
    rtos_unlock_mutex(&s_runtime_mutex);
    if (!cancelled) return;

    if (pending == XIAOTAI_OWNER_DEVICE_CALL) {
        reject_device_call(room_id, "rejected");
    } else {
        reject_voip_request(voip_request);
    }

    if (xiaotai_room_active(&s_room)) {
        s_ui_page = UI_PAGE_ROOM;
        room_render();
    } else if (active == XIAOTAI_OWNER_AI) {
        xiaotai_ui_show_ai(s_ai_view.phase, s_ai_view.emotion,
                           s_ai_view.caption,
                           s_ai_view.caption_type == 1);
    } else if (active == XIAOTAI_OWNER_STREAM) {
        xiaotai_ui_show_status("REMOTE");
    } else {
        xiaotai_ui_show_status("READY");
    }
    BK_LOGI(TAG, "user rejected pending incoming owner=%d\n", (int)pending);
}

static void handle_intent(xiaotai_intent_t intent)
{
    input_activity();
    if (s_ui_page == UI_PAGE_WECHAT_QR &&
        intent == XIAOTAI_INTENT_PRIMARY) {
        rtos_lock_mutex(&s_runtime_mutex);
        bool idle = s_runtime.owner == XIAOTAI_OWNER_NONE;
        rtos_unlock_mutex(&s_runtime_mutex);
        if (idle) {
            s_qr_waiting_for_wechat = false;
            navigate_to(UI_PAGE_HOME);
            s_home_last_refresh_ms = 0U;
            BK_LOGI(TAG, "WeChat QR dismissed by KEY\n");
            return;
        }
    }
    if (s_ui_page == UI_PAGE_ROOM && xiaotai_room_joined(&s_room)) {
        if (intent == XIAOTAI_INTENT_PTT_PRESS) {
            if (!s_room_key_talking &&
                xiaotai_room_action(&s_room,
                                    XIAOTAI_ROOM_ACTION_TALK_START,
                                    NULL) == BK_OK) {
                s_room_key_talking = true;
            }
            return;
        }
        if (intent == XIAOTAI_INTENT_PTT_RELEASE) {
            if (s_room_key_talking) {
                s_room_key_talking = false;
                (void)xiaotai_room_action(&s_room,
                                          XIAOTAI_ROOM_ACTION_TALK_STOP,
                                          NULL);
            }
            return;
        }
        if (intent == XIAOTAI_INTENT_PTT_HOLD) {
            if (xiaotai_room_action(&s_room,
                                    XIAOTAI_ROOM_ACTION_TALK_START,
                                    NULL) == BK_OK) {
                s_room_key_talking = true;
            }
            return;
        }
    }
    if (intent == XIAOTAI_INTENT_PTT_HOLD) {
        return;
    }
    if (intent == XIAOTAI_INTENT_QUICK_CALL ||
        intent == XIAOTAI_INTENT_HOME_WECHAT_CALL) {
        rtos_lock_mutex(&s_runtime_mutex);
        bool incoming_waiting = xiaotai_runtime_has_incoming(&s_runtime);
        rtos_unlock_mutex(&s_runtime_mutex);
        if (incoming_waiting) {
            request_reject_pending();
            return;
        }
        if (xiaotai_room_active(&s_room)) {
            BK_LOGW(TAG, "KEY double click ignored: group room active\n");
            return;
        }
        rtos_lock_mutex(&s_runtime_mutex);
        bool idle = s_runtime.owner == XIAOTAI_OWNER_NONE;
        rtos_unlock_mutex(&s_runtime_mutex);
        if (!idle) {
            BK_LOGW(TAG, "KEY double click ignored: media owner busy\n");
            return;
        }
        if (intent == XIAOTAI_INTENT_HOME_WECHAT_CALL) {
            quick_call_first_wechat_contact();
        } else {
            quick_call_first_contact();
        }
        return;
    }
    if (intent != XIAOTAI_INTENT_PRIMARY) return;
    rtos_lock_mutex(&s_runtime_mutex);
    bool incoming_waiting = xiaotai_runtime_has_incoming(&s_runtime);
    rtos_unlock_mutex(&s_runtime_mutex);
    if (incoming_waiting) {
        request_accept_pending();
        return;
    }
    if (xiaotai_room_active(&s_room)) {
        /* A room short press exits local media; it must not be replayed as
         * an idle primary action, which would incorrectly start AI. */
        return_home_from_room();
        return;
    }
    rtos_lock_mutex(&s_runtime_mutex);
    if (s_runtime.owner == XIAOTAI_OWNER_WECHAT_VOIP) {
        uint32_t generation = s_runtime.generation;
        bool incoming = s_runtime.state == XIAOTAI_STATE_CALL_INCOMING;
        bool active = s_runtime.state == XIAOTAI_STATE_CALL_ACTIVE ||
                      s_runtime.state == XIAOTAI_STATE_CALL_CONNECTING;
        bool connected = active && xiaotai_tirtc_connected();
        char room_id[sizeof(s_calls.voip.room_id)];
        char call_id[sizeof(s_calls.voip.call_id)];
        bool outbound_wait = s_calls.voip.outbound;
        snprintf(room_id, sizeof(room_id), "%s", s_calls.voip.room_id);
        snprintf(call_id, sizeof(call_id), "%s", s_calls.voip.call_id);
        if (incoming) {
            (void)xiaotai_runtime_connected(&s_runtime, generation);
            arm_runtime_timeout_locked(generation,
                                       SESSION_CONNECT_TIMEOUT_MS);
            os_free(s_voip_reject_request);
            s_voip_reject_request = NULL;
        } else if (active) {
            (void)xiaotai_runtime_begin_ending(
                &s_runtime, generation, rtos_get_time(),
                SESSION_END_TIMEOUT_MS);
        }
        rtos_unlock_mutex(&s_runtime_mutex);
        if (incoming) {
            /* The descriptor can approach 1 KB and TiRtcWhipConnect has a
             * deep synchronous JSON/HTTP/SDP call chain.  Keep credentials
             * in the session buffers: copying both onto the 12 KB control
             * stack leaves too little headroom and the SDK may retain them
             * until its asynchronous callback. */
            int rc = xiaotai_tirtc_voip_connect(
                s_calls.voip.peer_id, s_calls.voip.token, generation);
            if (rc != 0) {
                BK_LOGE(TAG, "VoIP answer connect failed rc=%d\n", rc);
                finish_voip(generation, rc);
            } else {
                xiaotai_ui_show_status("VOIP CONNECT");
                BK_LOGI(TAG, "KEY accepted VoIP room=%s\n", room_id);
            }
        } else if (active) {
            static const char hangup[] = "{\"reason\":0}";
            xiaotai_ui_show_status("ENDING");
            if (connected) {
                (void)xiaotai_tirtc_send_command(0x2001U, hangup,
                                                  sizeof(hangup) - 1U);
                int disconnect_rc = xiaotai_tirtc_disconnect();
                if (disconnect_rc != 0) {
                    BK_LOGW(TAG,
                            "VoIP hangup disconnect submission failed rc=%d; bounded retry armed\n",
                            disconnect_rc);
                }
            } else {
                if (outbound_wait && call_id[0] != '\0') {
                    xiaotai_call_state_mark_voip_stale(
                        &s_calls, call_id, rtos_get_time() + 60000U);
                }
                (void)xiaotai_tirtc_disconnect();
                finish_voip(generation, 0);
            }
            BK_LOGI(TAG, "KEY requested VoIP hangup room=%s\n", room_id);
        }
        return;
    }
    if (s_runtime.owner == XIAOTAI_OWNER_DEVICE_CALL) {
        uint32_t generation = s_runtime.generation;
        bool incoming = s_runtime.state == XIAOTAI_STATE_CALL_INCOMING;
        bool active = s_runtime.state == XIAOTAI_STATE_CALL_ACTIVE ||
                      s_runtime.state == XIAOTAI_STATE_CALL_CONNECTING;
        bool media_active = s_runtime.state == XIAOTAI_STATE_CALL_ACTIVE;
        bool outbound = s_calls.device.outbound;
        char room_id[sizeof(s_calls.device.room_id)];
        char peer_id[sizeof(s_calls.device.peer_id)];
        snprintf(room_id, sizeof(room_id), "%s", s_calls.device.room_id);
        snprintf(peer_id, sizeof(peer_id), "%s", s_calls.device.peer_id);
        if (incoming) {
            (void)xiaotai_runtime_connected(&s_runtime, generation);
            arm_runtime_timeout_locked(generation,
                                       SESSION_CONNECT_TIMEOUT_MS);
        } else if (active) {
            (void)xiaotai_runtime_begin_ending(
                &s_runtime, generation, rtos_get_time(),
                SESSION_END_TIMEOUT_MS);
        }
        rtos_unlock_mutex(&s_runtime_mutex);
        if (incoming) {
            char request[320];
            int length = xiaotai_call_encode_device_info(
                request, sizeof(request), peer_id, room_id);
            int rc = length > 0 && (size_t)length < sizeof(request) ?
                xiaotai_platform_service_request("/v1/call/device/info",
                    request, call_info_response,
                    (void *)(uintptr_t)generation) : BK_FAIL;
            if (rc != 0) {
                BK_LOGE(TAG, "device call answer request failed rc=%d\n", rc);
                rtos_lock_mutex(&s_runtime_mutex);
                (void)xiaotai_runtime_finish(&s_runtime, generation);
                rtos_unlock_mutex(&s_runtime_mutex);
                xiaotai_ui_show_status("ERROR");
            } else {
                xiaotai_ui_show_status("DEVICE CONNECT");
                BK_LOGI(TAG, "KEY accepted device call room=%s\n", room_id);
            }
        } else if (active) {
            bool connected = xiaotai_tirtc_connected();
            char request[224];
            bool cancel = outbound && !media_active;
            int length = xiaotai_call_encode_device_end(
                request, sizeof(request), room_id, "hangup", cancel);
            xiaotai_ui_show_status("ENDING");
            int disconnect_rc = xiaotai_tirtc_disconnect();
            if (disconnect_rc != 0 && connected) {
                BK_LOGW(TAG,
                        "device hangup disconnect submission failed rc=%d; bounded retry armed\n",
                        disconnect_rc);
            }
            if (room_id[0] != '\0' && length > 0 &&
                (size_t)length < sizeof(request)) {
                (void)xiaotai_platform_service_request(
                    cancel ? "/v1/call/cancel" : "/v1/call/hangup",
                    request, service_response_ignored, NULL);
            }
            if (!connected) finish_call(generation, 0);
            BK_LOGI(TAG, "KEY requested device call hangup room=%s\n", room_id);
        }
        return;
    }
    if (s_runtime.owner == XIAOTAI_OWNER_AI) {
        uint32_t generation = s_runtime.generation;
        rtos_unlock_mutex(&s_runtime_mutex);
        BK_LOGI(TAG, "button requested AI stop generation=%u\n",
                (unsigned)generation);
        stop_ai(generation);
        return;
    }
    if (s_runtime.owner == XIAOTAI_OWNER_NONE && s_settings.microphone_muted) {
        rtos_unlock_mutex(&s_runtime_mutex);
        BK_LOGW(TAG, "AI start rejected: %s\n", XIAOTAI_AI_GLOBAL_MUTE_REASON);
        /* Keep the explanation visible across the periodic home refresh. */
        s_home_defer_until_ms = rtos_get_time() + 5000U;
        xiaotai_ui_show_status("AI MIC MUTED");
        return;
    }
    if (s_runtime.owner != XIAOTAI_OWNER_NONE || !xiaotai_tirtc_ready() ||
        xiaotai_tirtc_busy() ||
        !xiaotai_runtime_begin(&s_runtime, XIAOTAI_OWNER_AI, false)) {
        xiaotai_session_owner_t owner = s_runtime.owner;
        rtos_unlock_mutex(&s_runtime_mutex);
        BK_LOGW(TAG, "AI button ignored: runtime not ready owner=%d rtc=%d\n",
                owner, xiaotai_tirtc_ready());
        return;
    }
    uint32_t generation = s_runtime.generation;
    arm_runtime_timeout_locked(generation, SESSION_CONNECT_TIMEOUT_MS);
    rtos_unlock_mutex(&s_runtime_mutex);

    xiaotai_ui_show_status("AI CONNECT");
    int rc = xiaotai_platform_service_request("/v1/ai/token", NULL,
                                               ai_token_response,
                                               (void *)(uintptr_t)generation);
    if (rc != 0) {
        BK_LOGE(TAG, "AI token request failed rc=%d\n", rc);
        finish_ai(generation, rc);
    } else {
        BK_LOGI(TAG, "manual AI request submitted generation=%u\n",
                (unsigned)generation);
    }
}

static void room_input_render(void)
{
    if (s_ui_page == UI_PAGE_ROOM_JOIN_CODE) {
        xiaotai_ui_show_room_join_code(s_room_code_input);
    } else {
        xiaotai_ui_show_room_password(s_room_input_create, s_room_password_input);
    }
}

static void room_input_submit(void)
{
    char request[64];
    const char *password = s_room_password_input;
    int length = s_room_input_create ?
        snprintf(request, sizeof(request), "{\"password\":\"%s\"}", password) :
        snprintf(request, sizeof(request),
                 "{\"room_code\":\"%s\",\"password\":\"%s\"}",
                 s_room_code_input, password);
    int rc = length > 0 && (size_t)length < sizeof(request) ?
        xiaotai_room_action(&s_room, s_room_input_create ?
                            XIAOTAI_ROOM_ACTION_CREATE : XIAOTAI_ROOM_ACTION_JOIN,
                            request) : BK_ERR_PARAM;
    BK_LOGI(TAG, "room input submitted create=%d rc=%d\n",
            s_room_input_create ? 1 : 0, rc);
    if (rc == BK_OK) {
        memset(s_room_password_input, 0, sizeof(s_room_password_input));
        s_room_password_input_length = 0U;
        navigate_to(UI_PAGE_ROOM);
    }
}

static bool room_touch_released(uint32_t generation)
{
    return generation != 0U && (int32_t)(
        atomic_load(&s_touch_release_generation) - generation) >= 0;
}

static void service_room_touch_release(void)
{
    if (!s_room_touch_talking || !room_touch_released(s_room_touch_generation)) return;
    s_room_touch_talking = false;
    (void)xiaotai_room_action(&s_room, XIAOTAI_ROOM_ACTION_TALK_STOP, NULL);
}

static void handle_touch(const control_event_t *event)
{
    bool was_off = !xiaotai_ui_backlight_on();
    input_activity();
    if (event->input_event == XIAOTAI_TOUCH_DOWN && was_off) {
        s_touch_wake_consumed = true;
        return;
    }
    if (event->input_event != XIAOTAI_TOUCH_DOWN &&
        s_touch_wake_consumed) {
        s_touch_wake_consumed = false;
        return;
    }
    if (event->input_event != XIAOTAI_TOUCH_DOWN &&
        s_touch_action_consumed) {
        s_touch_action_consumed = false;
        return;
    }
    /* A foreground transition (notably an incoming call) may happen while
     * the user is holding room PTT. Release the room uplink before routing
     * this gesture to the new foreground so speech can never remain latched. */
    if (event->input_event != XIAOTAI_TOUCH_DOWN &&
        s_room_touch_talking) {
        s_room_touch_talking = false;
        (void)xiaotai_room_action(&s_room,
                                  XIAOTAI_ROOM_ACTION_TALK_STOP, NULL);
        return;
    }
    if (event->input_event == XIAOTAI_TOUCH_CANCEL) return;
    rtos_lock_mutex(&s_runtime_mutex);
    bool incoming_waiting = xiaotai_runtime_has_incoming(&s_runtime);
    xiaotai_session_owner_t touch_owner = s_runtime.owner;
    bool call_active = s_runtime.state == XIAOTAI_STATE_CALL_ACTIVE &&
                       (touch_owner == XIAOTAI_OWNER_DEVICE_CALL ||
                        touch_owner == XIAOTAI_OWNER_WECHAT_VOIP);
    rtos_unlock_mutex(&s_runtime_mutex);
    if (incoming_waiting) {
        xiaotai_ui_action_t action = event->input_event == XIAOTAI_TOUCH_UP ?
            xiaotai_ui_incoming_action(event->x, event->y) :
            XIAOTAI_UI_ACTION_NONE;
        if (action == XIAOTAI_UI_ACTION_CALL_ACCEPT) {
            request_accept_pending();
        } else if (action == XIAOTAI_UI_ACTION_CALL_REJECT) {
            request_reject_pending();
        }
        return;
    }
    if (call_active) {
        xiaotai_ui_action_t action = event->input_event == XIAOTAI_TOUCH_UP ?
            xiaotai_ui_active_call_action(event->x, event->y) :
            XIAOTAI_UI_ACTION_NONE;
        if (action == XIAOTAI_UI_ACTION_CALL_MIC_TOGGLE) {
            s_call_microphone_muted = !s_call_microphone_muted;
            xiaotai_audio_set_microphone_muted(s_call_microphone_muted);
            xiaotai_ui_show_call_active(
                touch_owner == XIAOTAI_OWNER_WECHAT_VOIP,
                s_call_microphone_muted);
            BK_LOGI(TAG, "call microphone muted=%d\n",
                    s_call_microphone_muted ? 1 : 0);
        } else if (action == XIAOTAI_UI_ACTION_CALL_HANGUP) {
            handle_intent(XIAOTAI_INTENT_PRIMARY);
        }
        return;
    }
    if (s_ui_page == UI_PAGE_ROOM_JOIN_CODE ||
        s_ui_page == UI_PAGE_ROOM_PASSWORD) {
        if (event->input_event != XIAOTAI_TOUCH_DOWN) return;
        bool password = s_ui_page == UI_PAGE_ROOM_PASSWORD;
        s_touch_action_consumed = true;
        if (event->x < XIAOTAI_UI_BACK_HIT_X &&
            event->y < XIAOTAI_UI_BACK_HIT_Y) {
            memset(s_room_password_input, 0, sizeof(s_room_password_input));
            s_room_password_input_length = 0U;
            navigate_to(password && !s_room_input_create ?
                        UI_PAGE_ROOM_JOIN_CODE : UI_PAGE_ROOM);
            if (s_ui_page == UI_PAGE_ROOM) room_render();
            else room_input_render();
            return;
        }
        char *input = password ? s_room_password_input : s_room_code_input;
        size_t *length = password ? &s_room_password_input_length :
                                    &s_room_code_input_length;
        size_t limit = password ? 4U : 6U;
        int key = xiaotai_ui_room_keypad_key(event->x, event->y);
        if (key >= 0 && key <= 9 && *length < limit) {
            input[(*length)++] = (char)('0' + key);
            input[*length] = '\0';
        } else if (key == XIAOTAI_UI_ROOM_KEY_DELETE) {
            if (*length > 0U) input[--(*length)] = '\0';
        } else if (key == XIAOTAI_UI_ROOM_KEY_SUBMIT &&
                   (*length == limit ||
                    (password && !s_room_input_create && *length == 0U))) {
            if (!password) {
                memset(s_room_password_input, 0, sizeof(s_room_password_input));
                s_room_password_input_length = 0U;
                navigate_to(UI_PAGE_ROOM_PASSWORD);
            } else {
                room_input_submit();
            }
        }
        if (s_ui_page == UI_PAGE_ROOM) room_render();
        else room_input_render();
        return;
    }
    if (s_ui_page == UI_PAGE_ROOM_LEAVE_CONFIRM) {
        if (event->input_event != XIAOTAI_TOUCH_DOWN) return;
        xiaotai_ui_action_t action = xiaotai_ui_room_leave_confirm_action(
            event->x, event->y);
        if (action == XIAOTAI_UI_ACTION_ROOM_LEAVE_CANCEL) {
            s_touch_action_consumed = true;
            navigate_to(UI_PAGE_ROOM);
            room_render();
        } else if (action == XIAOTAI_UI_ACTION_ROOM_LEAVE_CONFIRM) {
            s_touch_action_consumed = true;
            int rc = xiaotai_room_action(&s_room,
                                         XIAOTAI_ROOM_ACTION_LEAVE, NULL);
            BK_LOGI(TAG, "room leave confirmed rc=%d\n", rc);
            if (rc == BK_OK) {
                s_ui_page = UI_PAGE_HOME;
                s_home_last_refresh_ms = 0U;
                xiaotai_ui_show_status("READY");
            }
        }
        return;
    }
    if (s_ui_page == UI_PAGE_ROOM) {
        xiaotai_room_snapshot_t snapshot;
        xiaotai_room_snapshot(&s_room, &snapshot);
        if (!snapshot.assigned) {
            if (event->input_event != XIAOTAI_TOUCH_DOWN) return;
            xiaotai_ui_action_t action = xiaotai_ui_room_entry_action(
                event->x, event->y);
            if (action == XIAOTAI_UI_ACTION_BACK) {
                s_touch_action_consumed = true;
                return_home_from_room();
            } else if (action == XIAOTAI_UI_ACTION_ROOM_CREATE &&
                       !snapshot.request_pending) {
                static const char request[] = "{\"password\":\"\"}";
                int rc = xiaotai_room_action(&s_room,
                    XIAOTAI_ROOM_ACTION_CREATE, request);
                BK_LOGI(TAG, "open room create submitted rc=%d\n", rc);
                s_touch_action_consumed = true;
                room_render();
            } else if (action == XIAOTAI_UI_ACTION_ROOM_CREATE_PASSWORD &&
                       !snapshot.request_pending) {
                s_room_input_create = true;
                memset(s_room_password_input, 0, sizeof(s_room_password_input));
                s_room_password_input_length = 0U;
                s_touch_action_consumed = true;
                navigate_to(UI_PAGE_ROOM_PASSWORD);
                room_input_render();
            } else if (action == XIAOTAI_UI_ACTION_ROOM_JOIN &&
                       !snapshot.request_pending) {
                s_room_input_create = false;
                memset(s_room_code_input, 0, sizeof(s_room_code_input));
                s_room_code_input_length = 0U;
                s_touch_action_consumed = true;
                navigate_to(UI_PAGE_ROOM_JOIN_CODE);
                xiaotai_ui_show_room_join_code(s_room_code_input);
            }
            return;
        }
    }
    if (s_ui_page == UI_PAGE_ROOM &&
        event->input_event == XIAOTAI_TOUCH_DOWN) {
        xiaotai_ui_action_t action = xiaotai_ui_room_action(
            event->x, event->y, true);
        if (action == XIAOTAI_UI_ACTION_BACK &&
            xiaotai_ui_action_triggers_on_down(action)) {
            return_home_from_room();
            s_touch_action_consumed = true;
            return;
        }
        if (action == XIAOTAI_UI_ACTION_ROOM_LEAVE &&
            xiaotai_ui_action_triggers_on_down(action)) {
            navigate_to(UI_PAGE_ROOM_LEAVE_CONFIRM);
            s_touch_action_consumed = true;
            xiaotai_ui_show_room_leave_confirm();
            return;
        }
    }
    if (s_ui_page == UI_PAGE_ROOM && xiaotai_room_joined(&s_room)) {
        if (event->input_event == XIAOTAI_TOUCH_DOWN) {
            xiaotai_ui_action_t action = xiaotai_ui_room_action(
                event->x, event->y, true);
            if (action == XIAOTAI_UI_ACTION_ROOM_TALK_START &&
                !room_touch_released(event->generation) &&
                xiaotai_room_action(&s_room,
                                    XIAOTAI_ROOM_ACTION_TALK_START,
                                    NULL) == BK_OK) {
                s_room_touch_generation = event->generation;
                s_room_touch_talking = true;
            }
            return;
        }
    }
    if (s_ui_page == UI_PAGE_ROOM &&
        event->input_event == XIAOTAI_TOUCH_UP) {
        xiaotai_ui_action_t action = xiaotai_ui_room_action(
            event->x, event->y, false);
        size_t pages = xiaotai_ui_room_page_count(
            s_room_snapshot.participant_count);
        if (action == XIAOTAI_UI_ACTION_ROOM_PAGE_PREV) {
            if (s_room_participant_page > 0U) --s_room_participant_page;
            room_render();
            return;
        }
        if (action == XIAOTAI_UI_ACTION_ROOM_PAGE_NEXT) {
            if (s_room_participant_page + 1U < pages) {
                ++s_room_participant_page;
            }
            room_render();
            return;
        }
    }
    if (s_ui_page == UI_PAGE_HOME &&
        event->input_event == XIAOTAI_TOUCH_DOWN &&
        xiaotai_ui_action_triggers_on_down(
            xiaotai_ui_home_action(event->x, event->y)) &&
        xiaotai_ui_home_action(event->x, event->y) ==
            XIAOTAI_UI_ACTION_HOME_QUICK_CALL) {
        s_touch_action_consumed = true;
        handle_intent(XIAOTAI_INTENT_HOME_WECHAT_CALL);
        return;
    }
    if (event->input_event == XIAOTAI_TOUCH_DOWN) return;
    if (s_ui_page == UI_PAGE_HOME &&
        event->input_event == XIAOTAI_TOUCH_MOVE) {
        s_clock_face = !s_clock_face;
        s_home_last_refresh_ms = 0U;
        return;
    }
    if (s_ui_page == UI_PAGE_RESET_CONFIRM) {
        if (s_reset_status == 1U) return;
        if (event->x < 40U && event->y < 34U) handle_reset_action(1U);
        else if (event->y >= 184U && event->y < 226U) {
            if (event->x >= 8U && event->x < 156U) handle_reset_action(1U);
            else if (event->x >= 164U && event->x < 312U) handle_reset_action(2U);
        }
        return;
    }
    if (s_ui_page == UI_PAGE_NETWORK && event->x < 40U && event->y < 34U) {
        navigate_to(UI_PAGE_SETTINGS);
        render_settings();
        return;
    }
    if (s_ui_page == UI_PAGE_DIAGNOSTICS && event->y >= 36U && event->y < 68U) {
        for (unsigned tab = 0U; tab < 3U; ++tab) {
            unsigned x = 14U + tab * 101U;
            if (event->x >= x && event->x < x + 90U) {
                s_diagnostics_tab = tab;
                render_diagnostics();
                break;
            }
        }
        return;
    }
    if (event->y < 40U && s_ui_page != UI_PAGE_HOME) {
        navigate_to(s_ui_page == UI_PAGE_LAUNCHER ? UI_PAGE_HOME :
                                                    UI_PAGE_LAUNCHER);
        if (s_ui_page == UI_PAGE_HOME) s_home_last_refresh_ms = 0U;
        else xiaotai_ui_show_launcher(s_launcher_selected);
        return;
    }

    if (s_ui_page == UI_PAGE_HOME) {
        xiaotai_ui_action_t action =
            xiaotai_ui_home_action(event->x, event->y);
        if (action == XIAOTAI_UI_ACTION_HOME_MENU) {
            navigate_to(UI_PAGE_LAUNCHER);
            s_launcher_selected = 6U;
            xiaotai_ui_show_launcher(s_launcher_selected);
        } else if (action == XIAOTAI_UI_ACTION_HOME_QUICK_CALL) {
            handle_intent(XIAOTAI_INTENT_HOME_WECHAT_CALL);
        } else {
            handle_intent(XIAOTAI_INTENT_PRIMARY);
        }
        return;
    }
    if (s_ui_page == UI_PAGE_LAUNCHER && event->y >= 42U &&
        event->y < 234U) {
        unsigned column = event->x >= 160U;
        unsigned row = (event->y - 42U) / 64U;
        unsigned item = row * 2U + column;
        s_launcher_selected = item;
        if (open_launcher_item(item)) handle_intent(XIAOTAI_INTENT_PRIMARY);
        return;
    }
    if (s_ui_page == UI_PAGE_CONTACTS) {
        if (event->y >= 220U && s_contact_display_count > 4U) {
            size_t pages = (s_contact_display_count + 3U) / 4U;
            s_contact_page = (s_contact_page + 1U) % pages;
            render_contacts();
        } else if (event->y >= 43U && event->y < 220U) {
            size_t index = s_contact_page * 4U +
                           (size_t)((event->y - 43U) / 46U);
            if (index < s_contact_display_count) {
                s_contact_selected = s_contact_display_indices[index];
                s_ui_page = UI_PAGE_CONTACT_DETAIL;
                render_contact_detail();
            }
        }
        return;
    }
    if (s_ui_page == UI_PAGE_CONTACT_DETAIL) {
        if (event->y >= 195U && s_contact_selected < s_contacts.count) {
            const xiaotai_contact_t *contact =
                &s_contacts.entries[s_contact_selected];
            if (contact->online || contact->type == XIAOTAI_CONTACT_VOIP) {
                dial_contact(contact);
            }
        }
        return;
    }
    if (s_ui_page == UI_PAGE_EXPRESSIONS) {
        size_t count = xiaotai_ai_emotion_count();
        if (count == 0U) return;
        if (event->x < 110U) {
            s_expression_selected =
                (unsigned)(((size_t)s_expression_selected + count - 1U) %
                           count);
        } else if (event->x > 210U) {
            s_expression_selected =
                (unsigned)(((size_t)s_expression_selected + 1U) % count);
        } else {
            size_t index = (size_t)s_expression_selected % count;
            const char *emotion = xiaotai_ai_emotion_at(index);
            if (emotion != NULL) {
                xiaotai_ai_view_set_emotion(&s_ai_view, emotion);
            }
        }
        render_expressions();
        return;
    }
    if (s_ui_page == UI_PAGE_NETWORK) return;
    if (s_ui_page == UI_PAGE_SETTINGS) {
        xiaotai_ui_action_t action =
            xiaotai_ui_settings_action(event->x, event->y);
        bool changed = false;
        if (action == XIAOTAI_UI_ACTION_VOLUME_DOWN ||
            action == XIAOTAI_UI_ACTION_VOLUME_UP) {
            if (action == XIAOTAI_UI_ACTION_VOLUME_DOWN) {
                if (s_settings.volume > 0U) s_settings.volume--;
                xiaotai_audio_set_volume(s_settings.volume);
                changed = true;
            } else if (action == XIAOTAI_UI_ACTION_VOLUME_UP) {
                if (s_settings.volume < 10U) s_settings.volume++;
                xiaotai_audio_set_volume(s_settings.volume);
                changed = true;
            }
        } else if (action == XIAOTAI_UI_ACTION_SPEAKER_TOGGLE) {
            s_settings.speaker_muted = !s_settings.speaker_muted;
            xiaotai_audio_set_speaker_muted(s_settings.speaker_muted);
            changed = true;
        } else if (action == XIAOTAI_UI_ACTION_MIC_TOGGLE) {
            s_settings.microphone_muted = !s_settings.microphone_muted;
            xiaotai_audio_set_microphone_muted(s_settings.microphone_muted);
            changed = true;
        } else if (action == XIAOTAI_UI_ACTION_MIC_SENSITIVITY_DOWN ||
                   action == XIAOTAI_UI_ACTION_MIC_SENSITIVITY_UP) {
            uint8_t next = s_settings.microphone_sensitivity;
            if (action == XIAOTAI_UI_ACTION_MIC_SENSITIVITY_DOWN) {
                if (next > 1U) next--;
            } else if (next < XIAOTAI_MIC_SENSITIVITY_MAX) next++;
            if (next == s_settings.microphone_sensitivity) return;
            if (!xiaotai_audio_commit_sensitivity(
                    &s_settings.microphone_sensitivity, next,
                    apply_microphone_sensitivity, NULL)) {
                BK_LOGE(TAG,
                        "microphone sensitivity change rejected current=%u requested=%u\n",
                        s_settings.microphone_sensitivity, next);
                xiaotai_ui_show_status("MIC ERROR");
                return;
            }
            changed = true;
        } else if (action == XIAOTAI_UI_ACTION_SLEEP) {
            s_settings.screen_timeout_index =
                (uint8_t)((s_settings.screen_timeout_index + 1U) % 5U);
            changed = true;
        }
        if (action == XIAOTAI_UI_ACTION_RESET) {
            handle_reset_action(0U);
            return;
        }
        if (action == XIAOTAI_UI_ACTION_NETWORK) {
            navigate_to(UI_PAGE_NETWORK);
            render_network();
            return;
        }
        if (!changed) return;
        (void)xiaotai_storage_save_settings(&s_settings);
        render_settings();
    }
}

static void query_string_value(const char *service_desc, const char *name,
                               char *output, size_t capacity)
{
    if (service_desc == NULL || name == NULL || output == NULL ||
        capacity == 0U) return;
    output[0] = '\0';
    const char *query = strchr(service_desc, '?');
    if (query == NULL) return;
    ++query;
    size_t name_length = strlen(name);
    while (*query != '\0') {
        const char *end = strchr(query, '&');
        if (end == NULL) end = query + strlen(query);
        const char *equals = memchr(query, '=', (size_t)(end - query));
        if (equals != NULL && (size_t)(equals - query) == name_length &&
            memcmp(query, name, name_length) == 0) {
            size_t value_length = (size_t)(end - equals - 1);
            if (value_length > 0U && value_length < capacity) {
                memcpy(output, equals + 1, value_length);
                output[value_length] = '\0';
            }
            return;
        }
        query = *end == '&' ? end + 1 : end;
    }
}

/* A short non-cryptographic tag lets field logs tell whether the platform
 * returned the same cached WHIP credential after a failed attempt.  It is not
 * reversible and must never be used as authentication material. */
static uint32_t credential_tag(const char *value)
{
    uint32_t hash = 2166136261U;
    if (value == NULL) return 0U;
    while (*value != '\0') {
        hash ^= (uint8_t)*value++;
        hash *= 16777619U;
    }
    return hash;
}

static const char *service_descriptor_scheme(const char *value)
{
    if (value == NULL) return "missing";
    const char *separator = strstr(value, "://");
    if (separator == NULL) return "opaque";
    size_t length = (size_t)(separator - value);
    if (length == 4U && strncmp(value, "http", length) == 0) return "http";
    if (length == 5U && strncmp(value, "https", length) == 0) return "https";
    if (length == 4U && strncmp(value, "whip", length) == 0) return "whip";
    if (length == 5U && strncmp(value, "whips", length) == 0) return "whips";
    return "other";
}

static void handle_ai_token(control_event_t *event)
{
    rtos_lock_mutex(&s_runtime_mutex);
    bool current = s_runtime.owner == XIAOTAI_OWNER_AI &&
                   s_runtime.state == XIAOTAI_STATE_AI_CONNECTING &&
                   s_runtime.generation == event->generation;
    rtos_unlock_mutex(&s_runtime_mutex);
    if (!current) return;

    cJSON *root = event->text == NULL ? NULL : cJSON_Parse(event->text);
    const cJSON *code = root == NULL ? NULL :
        cJSON_GetObjectItemCaseSensitive(root, "code");
    const cJSON *data = root == NULL ? NULL :
        cJSON_GetObjectItemCaseSensitive(root, "data");
    const cJSON *peer = cJSON_IsObject(data) ?
        cJSON_GetObjectItemCaseSensitive(data, "peer_id") : NULL;
    const cJSON *token = cJSON_IsObject(data) ?
        cJSON_GetObjectItemCaseSensitive(data, "token") : NULL;
    const cJSON *role = cJSON_IsObject(data) ?
        cJSON_GetObjectItemCaseSensitive(data, "role_id") : NULL;
    bool valid = cJSON_IsNumber(code) &&
                 (code->valueint == 0 || code->valueint == 200) &&
                 cJSON_IsString(peer) && peer->valuestring != NULL &&
                 cJSON_IsString(token) && token->valuestring != NULL;
    if (!valid) {
        cJSON_Delete(root);
        BK_LOGE(TAG, "AI token response invalid\n");
        finish_ai(event->generation, BK_FAIL);
        return;
    }
    size_t peer_length = strlen(peer->valuestring);
    size_t token_length = strlen(token->valuestring);
    if (peer_length >= sizeof(s_ai_peer_id) ||
        token_length >= sizeof(s_ai_token)) {
        cJSON_Delete(root);
        BK_LOGE(TAG, "AI credentials exceed retained buffer peer=%u token=%u\n",
                (unsigned)peer_length, (unsigned)token_length);
        finish_ai(event->generation, BK_ERR_NO_MEM);
        return;
    }
    /* TiRtcWhipConnect is asynchronous. Keep both strings alive until its
     * callback instead of passing pointers owned by the JSON parse tree.
     * `whips://` is a TiRTC service descriptor, not an HTTPS URL. It must be
     * forwarded byte-for-byte even when the SDK's HTTP control plane is
     * configured for plain transport. */
    memcpy(s_ai_peer_id, peer->valuestring, peer_length + 1U);
    memcpy(s_ai_token, token->valuestring, token_length + 1U);
    query_string_value(s_ai_peer_id, "role_id", s_ai_role_id,
                       sizeof(s_ai_role_id));
    if (s_ai_role_id[0] == '\0' && cJSON_IsString(role) &&
        role->valuestring != NULL) {
        snprintf(s_ai_role_id, sizeof(s_ai_role_id), "%s",
                 role->valuestring);
    }
    BK_LOGI(TAG,
            "AI credentials accepted peer-scheme=%s peer-len=%u token-len=%u role-len=%u credential-tag=%08x\n",
            service_descriptor_scheme(s_ai_peer_id),
            (unsigned)strlen(s_ai_peer_id),
            (unsigned)token_length,
            (unsigned)strlen(s_ai_role_id),
            (unsigned)credential_tag(s_ai_token));
    int rc = xiaotai_tirtc_ai_connect(s_ai_peer_id,
                                      s_ai_token,
                                      event->generation);
    cJSON_Delete(root);
    if (rc != 0) {
        BK_LOGE(TAG, "AI WHIP submission failed rc=%d\n", rc);
        finish_ai(event->generation, rc);
    }
}

static const char *json_string(const cJSON *object, const char *name)
{
    const cJSON *item = cJSON_IsObject(object) ?
        cJSON_GetObjectItemCaseSensitive(object, name) : NULL;
    return cJSON_IsString(item) && item->valuestring != NULL ?
        item->valuestring : "";
}

static void room_render(void)
{
    xiaotai_room_snapshot_t snapshot;
    xiaotai_room_snapshot(&s_room, &snapshot);
    if (s_room_snapshot_mutex != NULL) {
        rtos_lock_mutex(&s_room_snapshot_mutex);
        s_room_snapshot = snapshot;
        rtos_unlock_mutex(&s_room_snapshot_mutex);
    }
    if (s_ui_page == UI_PAGE_ROOM) {
        if (!snapshot.assigned) {
            xiaotai_ui_show_room_entry(snapshot.request_pending);
        } else {
            s_room_participant_page = xiaotai_ui_room_page_clamp(
                s_room_participant_page, snapshot.participant_count);
            xiaotai_ui_show_room(&snapshot, s_room_participant_page);
        }
    }
}

static void room_post_response(xiaotai_room_response_t type,
                               uint32_t generation, const char *text,
                               size_t length, void *context)
{
    (void)context;
    control_event_type_t event_type = CONTROL_ROOM_ASSIGNMENT;
    if (type == XIAOTAI_ROOM_RESPONSE_TOKEN) event_type = CONTROL_ROOM_TOKEN;
    else if (type == XIAOTAI_ROOM_RESPONSE_PRESENCE) {
        event_type = CONTROL_ROOM_PRESENCE;
    }
    queue_text_event(event_type, generation, text, length);
}

typedef struct {
    char *path;
    char *body;
    xiaotai_room_service_response_fn callback;
    void *callback_context;
} room_service_work_t;

static bool wait_for_room_transport_release(void)
{
    uint32_t deadline = rtos_get_time() + ROOM_TRANSPORT_RELEASE_TIMEOUT_MS;
    while (xiaotai_tirtc_connected() &&
           (int32_t)(rtos_get_time() - deadline) < 0) {
        rtos_delay_milliseconds(20U);
    }
    return !xiaotai_tirtc_connected();
}

static void room_service_task(beken_thread_arg_t argument)
{
    static const char failed_response[] = "{\"code\":-1}";
    room_service_work_t *work = (room_service_work_t *)argument;
    bool released = wait_for_room_transport_release();
    int rc = released ?
        xiaotai_platform_service_request(work->path, work->body,
                                         work->callback,
                                         work->callback_context) : BK_FAIL;
    if (rc != BK_OK) {
        BK_LOGW(TAG, "deferred room service failed path=%s released=%d rc=%d\n",
                work->path, released ? 1 : 0, rc);
        work->callback(failed_response, work->callback_context);
    }
    psram_free(work->body);
    psram_free(work->path);
    psram_free(work);
    rtos_delete_thread(NULL);
}

static char *room_service_copy(const char *value)
{
    if (value == NULL) return NULL;
    size_t size = strlen(value) + 1U;
    char *copy = psram_malloc(size);
    if (copy != NULL) memcpy(copy, value, size);
    return copy;
}

static bool room_service_defer_until_transport_closed(const char *path,
                                                       const char *body)
{
    if (strcmp(path, "/v1/call/group/device/leave") == 0) return true;
    if (strcmp(path, "/v1/call/group/device/presence") != 0 || body == NULL) {
        return false;
    }
    return strstr(body, "\"state\":\"suspended\"") != NULL ||
           strstr(body, "\"state\":\"ended\"") != NULL ||
           strstr(body, "\"state\":\"left\"") != NULL;
}

static int room_service_request(const char *path, const char *body,
                                xiaotai_room_service_response_fn callback,
                                void *callback_context, void *context)
{
    (void)context;
    if (path == NULL || callback == NULL) return BK_ERR_PARAM;
    if (room_service_defer_until_transport_closed(path, body)) {
        room_service_work_t *work = psram_malloc(sizeof(*work));
        if (work == NULL) return BK_ERR_NO_MEM;
        memset(work, 0, sizeof(*work));
        work->path = room_service_copy(path);
        work->body = room_service_copy(body);
        work->callback = callback;
        work->callback_context = callback_context;
        if (work->path == NULL || (body != NULL && work->body == NULL)) {
            psram_free(work->body);
            psram_free(work->path);
            psram_free(work);
            return BK_ERR_NO_MEM;
        }
        int rc = rtos_create_psram_thread(NULL, 4, "xiaotai_room_http",
                                          room_service_task,
                                          ROOM_SERVICE_TASK_STACK_SIZE, work);
        if (rc != BK_OK) {
            psram_free(work->body);
            psram_free(work->path);
            psram_free(work);
        }
        return rc;
    }
    return xiaotai_platform_service_request(path, body, callback,
                                             callback_context);
}

static uint32_t room_now_ms(void *context)
{
    (void)context;
    return rtos_get_time();
}

static bool room_random_bytes(void *buffer, size_t size, void *context)
{
    (void)context;
    return bk_fill_rand(buffer, size) == BK_OK;
}

static bool room_service_ready(void *context)
{
    (void)context;
    return xiaotai_network_ready() && xiaotai_tirtc_ready();
}

static bool room_transport_busy(void *context)
{
    (void)context;
    return xiaotai_tirtc_busy();
}

static bool room_transport_connected(void *context)
{
    (void)context;
    return xiaotai_tirtc_connected();
}

static int room_transport_connect(const char *peer, const char *token,
                                  uint32_t generation, void *context)
{
    (void)context;
    return xiaotai_tirtc_room_connect(peer, token, generation);
}

static int room_transport_start_media(void *context)
{
    (void)context;
    return xiaotai_tirtc_room_start_media();
}

static int room_transport_send(uint32_t command, const void *data,
                               uint32_t length, void *context)
{
    (void)context;
    return xiaotai_tirtc_send_command(command, data, length);
}

static int room_transport_disconnect_adapter(void *context)
{
    (void)context;
    return xiaotai_tirtc_disconnect();
}

static void room_set_uplink_enabled(bool enabled, void *context)
{
    (void)context;
    xiaotai_audio_set_uplink_enabled(enabled);
    BK_LOGI(TAG, "room uplink enabled=%d\n", enabled ? 1 : 0);
}

static bool room_media_acquire(uint32_t *generation, void *context)
{
    (void)context;
    rtos_lock_mutex(&s_runtime_mutex);
    bool acquired = xiaotai_runtime_begin(&s_runtime,
                                           XIAOTAI_OWNER_ROOM, false);
    if (acquired && generation != NULL) *generation = s_runtime.generation;
    rtos_unlock_mutex(&s_runtime_mutex);
    return acquired;
}

static bool room_media_activate(uint32_t generation, void *context)
{
    (void)context;
    rtos_lock_mutex(&s_runtime_mutex);
    bool active = s_runtime.owner == XIAOTAI_OWNER_ROOM &&
        xiaotai_runtime_media_started(&s_runtime, generation);
    rtos_unlock_mutex(&s_runtime_mutex);
    return active;
}

static void room_media_release(uint32_t generation, void *context)
{
    (void)context;
    rtos_lock_mutex(&s_runtime_mutex);
    if (s_runtime.owner == XIAOTAI_OWNER_ROOM) {
        (void)xiaotai_runtime_finish(&s_runtime, generation);
    }
    rtos_unlock_mutex(&s_runtime_mutex);
}

static bool room_media_available(void *context)
{
    (void)context;
    rtos_lock_mutex(&s_runtime_mutex);
    bool available = s_runtime.owner == XIAOTAI_OWNER_NONE ||
                     s_runtime.owner == XIAOTAI_OWNER_ROOM;
    rtos_unlock_mutex(&s_runtime_mutex);
    return available;
}

static void room_state_changed(void *context)
{
    (void)context;
    if (room_menu_page(s_ui_page)) room_render();
}

static void room_transport_closed(void *context)
{
    (void)context;
    s_room_key_talking = false;
    s_room_touch_talking = false;
    if (s_after_room_pending) {
        /* TiRTC invokes the closed callback before its rtc_thread has closed
         * every socket.  Starting the next call immediately can race the CP
         * Wi-Fi TX queue and corrupt an skb list. */
        s_after_room_closed_ms = rtos_get_time();
        s_after_room_transport_closed = true;
    } else {
        (void)show_pending_incoming();
    }
}

static void room_diagnostic(xiaotai_room_diagnostic_t event,
                            uint32_t generation, int detail, void *context)
{
    (void)context;
    if (event == XIAOTAI_ROOM_DIAG_DISCONNECTED) {
        BK_LOGI(TAG, "room transport closed generation=%u error=%d\n",
                (unsigned)generation, detail);
    } else if (event == XIAOTAI_ROOM_DIAG_LEAVE_FAILED) {
        BK_LOGW(TAG, "room leave failed; assignment retained\n");
        if (s_ui_page == UI_PAGE_HOME) {
            s_home_defer_until_ms = rtos_get_time() + 5000U;
            xiaotai_ui_show_status("ROOM LEAVE FAILED");
        }
    } else {
        BK_LOGW(TAG, "room diagnostic event=%d generation=%u detail=%d\n",
                (int)event, (unsigned)generation, detail);
    }
}

static void room_init(const char *device_id)
{
    const xiaotai_room_port_t port = {
        .service_request = room_service_request,
        .now_ms = room_now_ms,
        .random_bytes = room_random_bytes,
        .service_ready = room_service_ready,
        .transport_busy = room_transport_busy,
        .transport_connected = room_transport_connected,
        .transport_connect = room_transport_connect,
        .transport_start_media = room_transport_start_media,
        .transport_send = room_transport_send,
        .transport_disconnect = room_transport_disconnect_adapter,
        .set_uplink_enabled = room_set_uplink_enabled,
        .post_response = room_post_response,
        .media_acquire = room_media_acquire,
        .media_activate = room_media_activate,
        .media_release = room_media_release,
        .media_available = room_media_available,
        .state_changed = room_state_changed,
        .transport_closed = room_transport_closed,
        .diagnostic = room_diagnostic,
    };
    xiaotai_room_init(&s_room, device_id, &port);
    room_render();
}

static void handle_room_assignment(control_event_t *event)
{
    xiaotai_room_handle_response(&s_room,
        XIAOTAI_ROOM_RESPONSE_ASSIGNMENT, event->generation, event->text);
}

static void handle_room_token(control_event_t *event)
{
    xiaotai_room_handle_response(&s_room, XIAOTAI_ROOM_RESPONSE_TOKEN,
                                 event->generation, event->text);
}

enum {
    CONSOLE_ROOM_JOIN = XIAOTAI_ROOM_ACTION_JOIN,
    CONSOLE_ROOM_LEAVE = XIAOTAI_ROOM_ACTION_LEAVE,
    CONSOLE_ROOM_TALK_START = XIAOTAI_ROOM_ACTION_TALK_START,
    CONSOLE_ROOM_TALK_STOP = XIAOTAI_ROOM_ACTION_TALK_STOP,
    CONSOLE_ROOM_SYNC = XIAOTAI_ROOM_ACTION_SYNC,
};

static void handle_console_room(control_event_t *event)
{
    int rc = xiaotai_room_action(&s_room,
        (xiaotai_room_action_t)event->generation, event->text);
    if (rc != BK_OK) {
        BK_LOGW(TAG, "room action=%u rejected rc=%d\n",
                (unsigned)event->generation, rc);
    }
}

static void handle_room_connected(uint32_t generation)
{
    xiaotai_room_handle_connected(&s_room, generation);
}

static void handle_room_command(control_event_t *event)
{
    xiaotai_room_handle_command(&s_room, event->generation, event->text);
}

static void handle_room_disconnected(uint32_t generation, int error)
{
    xiaotai_room_handle_disconnected(&s_room, generation, error);
}

static void handle_room_presence(control_event_t *event)
{
    xiaotai_room_handle_response(&s_room, XIAOTAI_ROOM_RESPONSE_PRESENCE,
                                 event->generation, event->text);
}

static void handle_ai_ui(control_event_t *event)
{
    rtos_lock_mutex(&s_runtime_mutex);
    bool current = s_runtime.owner == XIAOTAI_OWNER_AI &&
                   s_runtime.generation == event->generation;
    rtos_unlock_mutex(&s_runtime_mutex);
    if (!current || event->text == NULL) return;

    char previous_emotion[sizeof(s_ai_view.emotion)];
    snprintf(previous_emotion, sizeof(previous_emotion), "%s",
             s_ai_view.emotion);
    bool present_now = xiaotai_ai_view_apply(&s_ai_view, event->text,
                                              rtos_get_time());
    if (strcmp(previous_emotion, s_ai_view.emotion) != 0) {
        BK_LOGI(TAG, "AI emotion changed %s -> %s\n",
                previous_emotion, s_ai_view.emotion);
    }
    if (present_now) {
        rtos_lock_mutex(&s_runtime_mutex);
        bool incoming_waiting = xiaotai_runtime_has_incoming(&s_runtime);
        rtos_unlock_mutex(&s_runtime_mutex);
        if (!incoming_waiting) {
            xiaotai_ui_show_ai(s_ai_view.phase, s_ai_view.emotion,
                               s_ai_view.caption,
                               s_ai_view.caption_type == 1);
        }
    }
}

static void handle_ai_end(uint32_t generation)
{
    rtos_lock_mutex(&s_runtime_mutex);
    bool current = s_runtime.owner == XIAOTAI_OWNER_AI &&
                   s_runtime.generation == generation;
    if (current) {
        xiaotai_ai_end_drain_begin(&s_ai_end_drain, generation,
                                   rtos_get_time(),
                                   AI_END_FINAL_AUDIO_ARRIVAL_MS,
                                   AI_END_DRAIN_TIMEOUT_MS);
    }
    rtos_unlock_mutex(&s_runtime_mutex);
    if (!current) return;
    xiaotai_audio_set_uplink_enabled(false);
    xiaotai_audio_log_playback_status("end-session");
    xiaotai_ui_show_status("ENDING");
    BK_LOGI(TAG,
            "AI end_session waiting for final playback generation=%u arrival-grace=%u timeout=%u ms\n",
            (unsigned)generation,
            (unsigned)AI_END_FINAL_AUDIO_ARRIVAL_MS,
            (unsigned)AI_END_DRAIN_TIMEOUT_MS);
}

static void finish_call(uint32_t generation, int error)
{
    rtos_lock_mutex(&s_runtime_mutex);
    bool finished = s_runtime.owner == XIAOTAI_OWNER_DEVICE_CALL &&
                    xiaotai_runtime_finish(&s_runtime, generation);
    if (finished) {
        s_ui_page = UI_PAGE_HOME;
        xiaotai_call_state_clear_device(&s_calls);
    }
    rtos_unlock_mutex(&s_runtime_mutex);
    if (finished) {
        bool normal_close = error == 0 ||
                            error == TIRTC_E_CONN_REMOTECLOSE;
        xiaotai_audio_set_microphone_muted(s_settings.microphone_muted);
        if (!normal_close) s_home_defer_until_ms = rtos_get_time() + 5000U;
        xiaotai_ui_show_status(normal_close ? "READY" : "ERROR");
        BK_LOGI(TAG, "device call finished generation=%u error=%d\n",
                (unsigned)generation, error);
    }
}

static void clear_voip_fields(void)
{
    os_free(s_voip_reject_request);
    s_voip_reject_request = NULL;
    xiaotai_call_state_clear_voip(&s_calls);
}

static void finish_voip(uint32_t generation, int error)
{
    rtos_lock_mutex(&s_runtime_mutex);
    bool finished = s_runtime.owner == XIAOTAI_OWNER_WECHAT_VOIP &&
                    xiaotai_runtime_finish(&s_runtime, generation);
    if (finished) clear_voip_fields();
    rtos_unlock_mutex(&s_runtime_mutex);
    if (finished) {
        bool normal_close = error == 0 ||
                            error == TIRTC_E_CONN_REMOTECLOSE;
        s_ui_page = UI_PAGE_HOME;
        xiaotai_audio_set_microphone_muted(s_settings.microphone_muted);
        if (!normal_close) s_home_defer_until_ms = rtos_get_time() + 5000U;
        xiaotai_ui_show_status(normal_close ? "READY" : "ERROR");
        BK_LOGI(TAG, "VoIP finished generation=%u error=%d\n",
                (unsigned)generation, error);
    }
}

static void handle_call_info(control_event_t *event)
{
    rtos_lock_mutex(&s_runtime_mutex);
    bool current = s_runtime.owner == XIAOTAI_OWNER_DEVICE_CALL &&
                   s_runtime.state == XIAOTAI_STATE_CALL_CONNECTING &&
                   s_runtime.generation == event->generation;
    char room_id[sizeof(s_calls.device.room_id)];
    snprintf(room_id, sizeof(room_id), "%s", s_calls.device.room_id);
    rtos_unlock_mutex(&s_runtime_mutex);
    if (!current) return;

    char peer_id[65] = {0};
    char token[1024] = {0};
    bool valid = xiaotai_call_decode_info(event->text, peer_id,
                                           sizeof(peer_id), token,
                                           sizeof(token));
    int rc = valid ? xiaotai_tirtc_call_connect(peer_id, token, room_id,
                                                 event->generation) : BK_FAIL;
    if (rc != 0) {
        BK_LOGE(TAG, "device call token/connect failed rc=%d\n", rc);
        finish_call(event->generation, rc);
    }
}

static void handle_contacts(control_event_t *event)
{
    int count = event->text == NULL ? -1 :
        xiaotai_contacts_replace_all_json(&s_contacts, event->text);
    if (count < 0) {
        BK_LOGW(TAG, "combined contact refresh invalid; preserving %u cached contacts\n",
                (unsigned)s_contacts.count);
    } else {
        unsigned devices = 0U;
        unsigned wechat = 0U;
        for (size_t i = 0U; i < s_contacts.count; ++i) {
            if (s_contacts.entries[i].type == XIAOTAI_CONTACT_VOIP) ++wechat;
            else ++devices;
        }
        BK_LOGI(TAG, "contacts refreshed total=%d device=%u WeChat=%u\n",
                count, devices, wechat);
        if (s_ui_page == UI_PAGE_CONTACTS) render_contacts();
        xiaotai_contact_t wechat_contact = {0};
        bool qr_target_available = s_qr_waiting_for_wechat ?
            xiaotai_contacts_first_of_type(
                &s_contacts, XIAOTAI_CONTACT_VOIP, &wechat_contact) ==
                XIAOTAI_CONTACT_MATCH_OK :
            s_contacts.count > 0U;
        if (s_ui_page == UI_PAGE_WECHAT_QR && qr_target_available) {
            rtos_lock_mutex(&s_runtime_mutex);
            bool idle = s_runtime.owner == XIAOTAI_OWNER_NONE;
            rtos_unlock_mutex(&s_runtime_mutex);
            if (idle) {
                s_ui_page = UI_PAGE_HOME;
                BK_LOGI(TAG,
                        "contact authorized; resuming pending quick call\n");
                if (s_qr_waiting_for_wechat) {
                    s_qr_waiting_for_wechat = false;
                    quick_call_first_wechat_contact();
                } else {
                    quick_call_first_contact();
                }
            }
        }
    }
}

static void finish_outbound_submission(uint32_t generation, int error)
{
    rtos_lock_mutex(&s_runtime_mutex);
    xiaotai_session_owner_t owner = s_runtime.owner;
    rtos_unlock_mutex(&s_runtime_mutex);
    if (owner == XIAOTAI_OWNER_DEVICE_CALL) finish_call(generation, error);
    else if (owner == XIAOTAI_OWNER_WECHAT_VOIP) finish_voip(generation, error);
}

static void finish_outbound_offline(uint32_t generation)
{
    finish_outbound_submission(generation, 0);
    s_home_defer_until_ms = rtos_get_time() + 5000U;
    xiaotai_ui_show_status("CONTACT OFFLINE");
    BK_LOGI(TAG, "outbound call target offline generation=%u\n",
            (unsigned)generation);
}

static void handle_outbound_call(control_event_t *event)
{
    rtos_lock_mutex(&s_runtime_mutex);
    bool current = s_runtime.generation == event->generation &&
                   s_runtime.state == XIAOTAI_STATE_CALL_CONNECTING &&
                   (s_runtime.owner == XIAOTAI_OWNER_DEVICE_CALL ||
                    s_runtime.owner == XIAOTAI_OWNER_WECHAT_VOIP);
    xiaotai_session_owner_t owner = s_runtime.owner;
    rtos_unlock_mutex(&s_runtime_mutex);
    if (!current) return;

    char value[128] = {0};
    xiaotai_call_outbound_result_t result =
        xiaotai_call_decode_outbound_result(
            event->text, owner == XIAOTAI_OWNER_WECHAT_VOIP,
            value, sizeof(value));
    bool valid = result == XIAOTAI_CALL_OUTBOUND_ACCEPTED;
    if (result == XIAOTAI_CALL_OUTBOUND_OFFLINE) {
        finish_outbound_offline(event->generation);
        return;
    }
    int rc = BK_FAIL;
    if (valid && owner == XIAOTAI_OWNER_DEVICE_CALL) {
        rtos_lock_mutex(&s_runtime_mutex);
        if (s_runtime.generation == event->generation &&
            s_runtime.owner == owner) {
            (void)xiaotai_call_state_set_device_room(&s_calls, value);
        }
        rtos_unlock_mutex(&s_runtime_mutex);
        rc = xiaotai_tirtc_expect_outbound_call(value, event->generation);
        if (rc == 0) {
            xiaotai_ui_show_status("DEVICE OUT");
            BK_LOGI(TAG, "outbound device call waiting room=%s target=%s\n",
                    value, s_calls.device.peer_id);
        }
    } else if (valid) {
        rtos_lock_mutex(&s_runtime_mutex);
        if (s_runtime.generation == event->generation &&
            s_runtime.owner == owner) {
            (void)xiaotai_call_state_set_voip_call_id(&s_calls, value);
            rc = 0;
        }
        rtos_unlock_mutex(&s_runtime_mutex);
        if (rc == 0) {
            xiaotai_ui_show_status("VOIP OUT");
            BK_LOGI(TAG, "outbound VoIP waiting call_id=%s\n", value);
        }
    }
    if (rc != 0) {
        const char *response = event->text == NULL ? "<empty>" : event->text;
        BK_LOGE(TAG,
                "outbound call request invalid or failed rc=%d response=%.160s\n",
                rc, response);
        finish_outbound_submission(event->generation, rc);
    }
}

static void dial_contact(const xiaotai_contact_t *contact)
{
    if (contact == NULL) return;
    xiaotai_session_owner_t owner = contact->type == XIAOTAI_CONTACT_DEVICE ?
        XIAOTAI_OWNER_DEVICE_CALL : XIAOTAI_OWNER_WECHAT_VOIP;
    rtos_lock_mutex(&s_runtime_mutex);
    bool started = xiaotai_runtime_begin(&s_runtime, owner, false);
    uint32_t generation = s_runtime.generation;
    if (started) {
        arm_runtime_timeout_locked(generation, SESSION_OUTGOING_TIMEOUT_MS);
        if (owner == XIAOTAI_OWNER_DEVICE_CALL) {
            started = xiaotai_call_state_begin_device_outbound(&s_calls,
                                                               contact->id);
        } else {
            clear_voip_fields();
            started = xiaotai_call_state_begin_voip_outbound(&s_calls,
                                                             contact->id);
        }
        if (!started) {
            (void)xiaotai_runtime_finish(&s_runtime, generation);
        }
    }
    rtos_unlock_mutex(&s_runtime_mutex);
    if (!started) {
        BK_LOGW(TAG, "contact dial ignored: media owner busy\n");
        return;
    }

    char request[512];
    int length;
    if (owner == XIAOTAI_OWNER_DEVICE_CALL) {
        length = xiaotai_call_encode_device_dial(request, sizeof(request),
                                                 contact->id);
    } else {
        length = xiaotai_call_encode_voip_dial(
            request, sizeof(request), s_device_id, contact->id,
            contact->model_id, contact->app_id);
    }
    const char *path = owner == XIAOTAI_OWNER_DEVICE_CALL ?
        "/v1/call/request" : "/v1/voip/device/call";
    int rc = length > 0 && (size_t)length < sizeof(request) ?
        xiaotai_platform_service_request(path, request,
            outbound_call_response, (void *)(uintptr_t)generation) : BK_FAIL;
    if (rc != 0) {
        BK_LOGE(TAG, "contact dial submission failed rc=%d\n", rc);
        finish_outbound_submission(generation, rc);
    } else {
        xiaotai_ui_show_status(owner == XIAOTAI_OWNER_DEVICE_CALL ?
                               "DEVICE CONNECT" : "VOIP CONNECT");
        BK_LOGI(TAG, "contact dial submitted name=%s type=%d\n",
                contact->name, (int)contact->type);
    }
}

static void handle_ai_call_intent(control_event_t *event)
{
    rtos_lock_mutex(&s_runtime_mutex);
    bool current = s_runtime.owner == XIAOTAI_OWNER_AI &&
                   s_runtime.generation == event->generation;
    rtos_unlock_mutex(&s_runtime_mutex);
    if (!current || event->text == NULL) return;

    xiaotai_contact_t contact = {0};
    xiaotai_contact_match_t match =
        xiaotai_contacts_resolve_call_intent_json(
            &s_contacts, event->text, &contact);
    if (match != XIAOTAI_CONTACT_MATCH_OK) {
        char response[512];
        const char *status = match == XIAOTAI_CONTACT_MATCH_AMBIGUOUS ?
            "ambiguous_target" :
            (match == XIAOTAI_CONTACT_MATCH_NOT_FOUND ?
                "not_found" : "invalid_action");
        const char *message = match == XIAOTAI_CONTACT_MATCH_AMBIGUOUS ?
            "找到多个同名联系人，请说明设备或微信联系人" :
            (match == XIAOTAI_CONTACT_MATCH_NOT_FOUND ?
                "未找到这个联系人" : "呼叫指令无效");
        int response_length = xiaotai_ai_encode_call_action_result(
            response, sizeof(response), event->text, false, status, message,
            NULL, NULL, NULL);
        if (response_length > 0) {
            (void)xiaotai_tirtc_send_command(
                XIAOTAI_AI_COMMAND, response, (uint32_t)response_length);
        }
        xiaotai_ui_show_status(match == XIAOTAI_CONTACT_MATCH_AMBIGUOUS ?
                               "DUP CONTACT" : "NO CONTACT");
        BK_LOGW(TAG, "AI call intent rejected match=%d cached=%u\n",
                (int)match, (unsigned)s_contacts.count);
        return;
    }

    char response[512];
    char message[96];
    const char *route = contact.type == XIAOTAI_CONTACT_DEVICE ?
        "device_call" : "wechat_voip";
    int message_length = snprintf(message, sizeof(message),
                                  "正在呼叫%s", contact.name);
    if (message_length <= 0 || (size_t)message_length >= sizeof(message)) {
        BK_LOGE(TAG, "AI call result message overflow\n");
        return;
    }
    int response_length = xiaotai_ai_encode_call_action_result(
        response, sizeof(response), event->text, true, "ok", message, route,
        contact.type == XIAOTAI_CONTACT_DEVICE ? contact.id : NULL,
        contact.name);
    if (response_length < 0) {
        BK_LOGE(TAG, "AI call action response encode failed\n");
        return;
    }
    if (response_length > 0 &&
        xiaotai_tirtc_send_command(
            XIAOTAI_AI_COMMAND, response, (uint32_t)response_length) < 0) {
        BK_LOGE(TAG, "AI call action response send failed\n");
        return;
    }

    rtos_lock_mutex(&s_runtime_mutex);
    s_pending_contact = contact;
    s_pending_dial_generation = event->generation;
    rtos_unlock_mutex(&s_runtime_mutex);
    static const char end[] =
        "{\"jsonrpc\":\"2.0\",\"method\":\"end_session\"}";
    (void)xiaotai_tirtc_send_command(XIAOTAI_AI_COMMAND, end,
                                     sizeof(end) - 1U);
    (void)xiaotai_tirtc_disconnect();
    xiaotai_ui_show_status("CALL PREP");
    BK_LOGI(TAG, "AI call intent accepted name=%s type=%d\n",
            contact.name, (int)contact.type);
}

static void handle_dial_contact(void)
{
    rtos_lock_mutex(&s_runtime_mutex);
    xiaotai_contact_t contact = s_pending_contact;
    bool ready = s_runtime.owner == XIAOTAI_OWNER_NONE &&
                 s_pending_dial_generation != 0U;
    s_pending_dial_generation = 0U;
    memset(&s_pending_contact, 0, sizeof(s_pending_contact));
    rtos_unlock_mutex(&s_runtime_mutex);
    if (ready) dial_contact(&contact);
}

static void handle_console_call(const control_event_t *event)
{
    bool wechat = event->error == XIAOTAI_CONTACT_VOIP;
    xiaotai_contact_t contact = {0};
    xiaotai_contact_match_t match = XIAOTAI_CONTACT_MATCH_NOT_FOUND;
    if (event->text != NULL && event->text[0] != '\0') {
        match = xiaotai_contacts_match(&s_contacts, event->text, NULL,
                                       wechat ? "wechat" : "device",
                                       &contact);
    } else if (wechat) {
        for (size_t i = 0; i < s_contacts.count; ++i) {
            if (s_contacts.entries[i].type == XIAOTAI_CONTACT_VOIP) {
                contact = s_contacts.entries[i];
                match = XIAOTAI_CONTACT_MATCH_OK;
                break;
            }
        }
    }
    if (match != XIAOTAI_CONTACT_MATCH_OK ||
        (!wechat && !contact.online)) {
        if (wechat && match == XIAOTAI_CONTACT_MATCH_NOT_FOUND) {
            show_wechat_qr_waiting(true);
        } else {
            xiaotai_ui_show_status(match == XIAOTAI_CONTACT_MATCH_AMBIGUOUS ?
                                   "DUP CONTACT" : "NO CONTACT");
        }
        BK_LOGW(TAG, "console %s call rejected match=%d\n",
                wechat ? "WeChat" : "device", (int)match);
        return;
    }
    dial_contact(&contact);
}

enum {
    CONSOLE_CALL_ANSWER = 1,
    CONSOLE_CALL_HANGUP,
};

static void handle_console_call_action(const control_event_t *event)
{
    rtos_lock_mutex(&s_runtime_mutex);
    xiaotai_session_owner_t owner = s_runtime.owner;
    xiaotai_runtime_state_t state = s_runtime.state;
    xiaotai_session_owner_t expected =
        (xiaotai_session_owner_t)event->error;
    xiaotai_session_owner_t pending = s_runtime.pending_incoming;
    bool pending_channel_ok = expected == XIAOTAI_OWNER_NONE ||
                              expected == pending;
    if (event->generation == CONSOLE_CALL_ANSWER &&
        pending != XIAOTAI_OWNER_NONE && pending_channel_ok) {
        rtos_unlock_mutex(&s_runtime_mutex);
        request_accept_pending();
        return;
    }
    if (event->generation == CONSOLE_CALL_HANGUP &&
        pending != XIAOTAI_OWNER_NONE && pending_channel_ok) {
        char room_id[sizeof(s_calls.device.room_id)] = {0};
        char *voip_request = NULL;
        if (pending == XIAOTAI_OWNER_DEVICE_CALL) {
            snprintf(room_id, sizeof(room_id), "%s", s_calls.device.room_id);
            xiaotai_call_state_clear_device(&s_calls);
        } else {
            voip_request = s_voip_reject_request;
            s_voip_reject_request = NULL;
            clear_voip_fields();
        }
        (void)xiaotai_runtime_cancel_incoming(&s_runtime, NULL);
        rtos_unlock_mutex(&s_runtime_mutex);
        if (pending == XIAOTAI_OWNER_DEVICE_CALL) {
            reject_device_call(room_id, "rejected");
        } else {
            reject_voip_request(voip_request);
        }
        BK_LOGI(TAG, "console rejected pending incoming owner=%d\n",
                (int)pending);
        return;
    }
    bool is_call = owner == XIAOTAI_OWNER_DEVICE_CALL ||
                   owner == XIAOTAI_OWNER_WECHAT_VOIP;
    bool channel_ok = expected == XIAOTAI_OWNER_NONE || expected == owner;
    if (event->generation == CONSOLE_CALL_HANGUP && is_call && channel_ok &&
        state == XIAOTAI_STATE_CALL_INCOMING) {
        uint32_t generation = s_runtime.generation;
        char room_id[sizeof(s_calls.device.room_id)] = {0};
        char *voip_request = NULL;
        if (owner == XIAOTAI_OWNER_DEVICE_CALL) {
            snprintf(room_id, sizeof(room_id), "%s", s_calls.device.room_id);
        } else {
            voip_request = s_voip_reject_request;
            s_voip_reject_request = NULL;
        }
        bool finished = xiaotai_runtime_finish(&s_runtime, generation);
        if (finished && owner == XIAOTAI_OWNER_DEVICE_CALL) {
            xiaotai_call_state_clear_device(&s_calls);
        } else if (finished) {
            clear_voip_fields();
        }
        rtos_unlock_mutex(&s_runtime_mutex);
        if (!finished) {
            os_free(voip_request);
            return;
        }
        if (owner == XIAOTAI_OWNER_DEVICE_CALL && room_id[0] != '\0') {
            reject_device_call(room_id, "rejected");
        } else {
            reject_voip_request(voip_request);
        }
        s_ui_page = UI_PAGE_HOME;
        xiaotai_ui_show_status("READY");
        BK_LOGI(TAG, "console rejected incoming call owner=%d generation=%u\n",
                (int)owner, (unsigned)generation);
        return;
    }
    rtos_unlock_mutex(&s_runtime_mutex);
    bool state_ok = event->generation == CONSOLE_CALL_ANSWER ?
                    state == XIAOTAI_STATE_CALL_INCOMING :
                    (state == XIAOTAI_STATE_CALL_CONNECTING ||
                     state == XIAOTAI_STATE_CALL_ACTIVE);
    if (is_call && channel_ok && state_ok) {
        handle_intent(XIAOTAI_INTENT_PRIMARY);
    } else {
        BK_LOGW(TAG,
                "console call action rejected action=%u owner=%d state=%d expected=%d\n",
                (unsigned)event->generation, (int)owner, (int)state,
                (int)expected);
    }
}

static void reject_device_call(const char *room_id, const char *reason)
{
    char request[224];
    int length = xiaotai_call_encode_device_end(
        request, sizeof(request), room_id, reason, false);
    if (length > 0 && (size_t)length < sizeof(request)) {
        (void)xiaotai_platform_service_request("/v1/call/reject", request,
                                                service_response_ignored,
                                                NULL);
    }
}

static char *build_voip_reject_request(const xiaotai_signal_view_t *signal,
                                       int reason)
{
    char *json = xiaotai_call_encode_voip_reject(
        signal->wx_app_id, signal->wx_model_id, signal->wx_server_token,
        signal->wx_room_id, signal->wx_payload, reason);
    if (json == NULL) {
        BK_LOGW(TAG, "VoIP reject skipped: notification lacks credentials\n");
    }
    return json;
}

static void reject_voip_request(char *request)
{
    if (request == NULL) return;
    (void)xiaotai_tirtc_service_request_json("/v1/wxvoip/reject", request,
                                              service_response_ignored,
                                              NULL);
    os_free(request);
}

static void reject_voip_call(const xiaotai_signal_view_t *signal, int reason)
{
    reject_voip_request(build_voip_reject_request(signal, reason));
}

static void handle_decoded_platform_signal(const xiaotai_signal_view_t *signal,
                                           void *context)
{
    (void)context;

    if (signal->type == XIAOTAI_SIGNAL_UNBIND) {
        BK_LOGW(TAG, "device unbound by platform; clearing credentials\n");
        if (xiaotai_storage_clear_device() == BK_OK) {
            rtos_delay_milliseconds(300);
            bk_reboot();
        }
        BK_LOGE(TAG, "cannot clear stored device credentials\n");
        return;
    } else if (signal->type == XIAOTAI_SIGNAL_DEVICE_CALL_INCOMING) {
        const char *room_id = signal->room_id;
        const char *caller_id = signal->caller_id;
        const char *call_type = signal->call_type;
        if (room_id[0] == '\0' || caller_id[0] == '\0') {
            BK_LOGW(TAG, "invalid device call notification\n");
        } else {
            rtos_lock_mutex(&s_runtime_mutex);
            bool accepted = xiaotai_runtime_offer_incoming(
                &s_runtime, XIAOTAI_OWNER_DEVICE_CALL, rtos_get_time(),
                SESSION_INCOMING_TIMEOUT_MS);
            if (accepted) {
                accepted = xiaotai_call_state_set_device_incoming(
                    &s_calls, room_id, caller_id, call_type);
                if (!accepted) {
                    (void)xiaotai_runtime_cancel_incoming(&s_runtime, NULL);
                }
            }
            rtos_unlock_mutex(&s_runtime_mutex);
            if (accepted) {
                xiaotai_ui_show_status("DEVICE CALL");
                BK_LOGI(TAG,
                        "device call incoming room=%s caller=%s type=%s; press KEY to answer\n",
                        room_id, caller_id, s_calls.device.type);
            } else {
                reject_device_call(room_id, "busy");
                BK_LOGW(TAG, "device call rejected busy room=%s\n", room_id);
            }
        }
    } else if (signal->type == XIAOTAI_SIGNAL_VOIP_CALL_INCOMING) {
        const char *peer_id = signal->peer_id;
        const char *token = signal->token;
        const char *room_id = signal->wx_room_id;
        const char *call_id = signal->wx_call_id;
        const char *openid = signal->wx_user_openid;
        const char *wx_from = signal->wx_from;
        xiaotai_voip_offer_t offer;
        rtos_lock_mutex(&s_runtime_mutex);
        bool outbound_pending =
            s_runtime.owner == XIAOTAI_OWNER_WECHAT_VOIP &&
            s_runtime.state == XIAOTAI_STATE_CALL_CONNECTING;
        offer = xiaotai_call_state_classify_voip(
            &s_calls, outbound_pending, rtos_get_time(), s_device_id,
            peer_id, token, room_id, call_id, openid, wx_from);
        rtos_unlock_mutex(&s_runtime_mutex);
        if (offer == XIAOTAI_VOIP_OFFER_INVALID) {
            BK_LOGW(TAG, "invalid VoIP call notification\n");
        } else {
            rtos_lock_mutex(&s_runtime_mutex);
            bool outbound = offer == XIAOTAI_VOIP_OFFER_OUTBOUND_MATCH;
            bool stale = offer == XIAOTAI_VOIP_OFFER_STALE;
            bool outbound_mismatch =
                offer == XIAOTAI_VOIP_OFFER_OUTBOUND_MISMATCH;
            bool accepted = false;
            uint32_t generation = s_runtime.generation;
            if (outbound) {
                accepted = xiaotai_call_state_accept_voip(
                    &s_calls, peer_id, token, room_id, call_id, true);
            } else if (!stale && !outbound_mismatch) {
                accepted = xiaotai_runtime_offer_incoming(
                    &s_runtime, XIAOTAI_OWNER_WECHAT_VOIP, rtos_get_time(),
                    SESSION_INCOMING_TIMEOUT_MS);
            }
            if (accepted && !outbound) {
                accepted = xiaotai_call_state_accept_voip(
                    &s_calls, peer_id, token, room_id, call_id, false);
                if (!accepted) {
                    (void)xiaotai_runtime_cancel_incoming(&s_runtime, NULL);
                }
            }
            rtos_unlock_mutex(&s_runtime_mutex);
            if (outbound) {
                int rc = xiaotai_tirtc_voip_connect(
                    s_calls.voip.peer_id, s_calls.voip.token, generation);
                if (rc != 0) finish_voip(generation, rc);
                else {
                    xiaotai_ui_show_status("VOIP CONNECT");
                    BK_LOGI(TAG, "outbound VoIP answered call_id=%s\n", call_id);
                }
            } else if (accepted) {
                os_free(s_voip_reject_request);
                s_voip_reject_request =
                    build_voip_reject_request(signal, 0);
                xiaotai_ui_show_status("VOIP CALL");
                BK_LOGI(TAG, "VoIP incoming room=%s; press KEY to answer\n",
                        room_id);
            } else {
                reject_voip_call(signal, outbound_mismatch ? 5 : 7);
                BK_LOGW(TAG, "VoIP rejected %s room=%s\n",
                        stale ? "late" :
                        (outbound_mismatch ? "outbound-mismatch" : "busy"),
                        room_id);
            }
        }
    } else if (signal->type == XIAOTAI_SIGNAL_CONTACTS_CHANGED) {
        refresh_contacts();
        BK_LOGI(TAG, "contact refresh scheduled by platform\n");
    } else if (signal->type == XIAOTAI_SIGNAL_ROOM_ASSIGNMENT_CHANGED) {
        xiaotai_room_assignment_changed(&s_room);
        BK_LOGI(TAG, "room assignment refresh scheduled by platform\n");
    } else if (signal->type == XIAOTAI_SIGNAL_ROOM_CLOSED) {
        xiaotai_room_closed(&s_room);
        BK_LOGI(TAG, "room closed by platform\n");
    } else if (signal->type == XIAOTAI_SIGNAL_CALL_ENDED) {
        const char *room_id = signal->room_id;
        const char *wx_room_id = signal->wx_room_id;
        rtos_lock_mutex(&s_runtime_mutex);
        bool device_matched = s_runtime.owner == XIAOTAI_OWNER_DEVICE_CALL &&
            xiaotai_call_state_matches_device(&s_calls, room_id);
        bool voip_matched = s_runtime.owner == XIAOTAI_OWNER_WECHAT_VOIP &&
            xiaotai_call_state_matches_voip(&s_calls, wx_room_id);
        bool pending_device =
            s_runtime.pending_incoming == XIAOTAI_OWNER_DEVICE_CALL &&
            xiaotai_call_state_matches_device(&s_calls, room_id);
        bool pending_voip =
            s_runtime.pending_incoming == XIAOTAI_OWNER_WECHAT_VOIP &&
            xiaotai_call_state_matches_voip(&s_calls, wx_room_id);
        if (pending_device || pending_voip) {
            (void)xiaotai_runtime_cancel_incoming(&s_runtime, NULL);
            if (pending_device) {
                xiaotai_call_state_clear_device(&s_calls);
            } else {
                clear_voip_fields();
            }
        }
        uint32_t generation = s_runtime.generation;
        rtos_unlock_mutex(&s_runtime_mutex);
        if (device_matched || voip_matched) {
            bool connected = xiaotai_tirtc_connected();
            (void)xiaotai_tirtc_disconnect();
            if (!connected && device_matched) finish_call(generation, 0);
            else if (!connected) finish_voip(generation, 0);
        } else if (pending_device || pending_voip) {
            if (xiaotai_room_active(&s_room)) room_render();
            else xiaotai_ui_show_status("READY");
        }
    } else if (signal->type == XIAOTAI_SIGNAL_UNKNOWN) {
        BK_LOGI(TAG, "platform command received type=%s\n", signal->name);
    }
}

static void handle_platform_signal(control_event_t *event)
{
    if (!xiaotai_signal_decode(event->text, handle_decoded_platform_signal,
                               NULL)) {
        BK_LOGW(TAG, "platform command ignored: invalid envelope\n");
    }
}

static void handle_runtime_timeout(void)
{
    xiaotai_session_owner_t pending_owner = XIAOTAI_OWNER_NONE;
    char pending_room_id[sizeof(s_calls.device.room_id)] = {0};
    char *pending_voip_reject = NULL;
    rtos_lock_mutex(&s_runtime_mutex);
    bool pending_expired = xiaotai_runtime_expire_incoming(
        &s_runtime, rtos_get_time(), &pending_owner);
    if (pending_expired && pending_owner == XIAOTAI_OWNER_DEVICE_CALL) {
        snprintf(pending_room_id, sizeof(pending_room_id), "%s",
                 s_calls.device.room_id);
        xiaotai_call_state_clear_device(&s_calls);
    } else if (pending_expired &&
               pending_owner == XIAOTAI_OWNER_WECHAT_VOIP) {
        pending_voip_reject = s_voip_reject_request;
        s_voip_reject_request = NULL;
        clear_voip_fields();
    }
    xiaotai_session_owner_t active_owner = s_runtime.owner;
    rtos_unlock_mutex(&s_runtime_mutex);
    if (pending_expired) {
        if (pending_owner == XIAOTAI_OWNER_DEVICE_CALL &&
            pending_room_id[0] != '\0') {
            reject_device_call(pending_room_id, "timeout");
        } else if (pending_owner == XIAOTAI_OWNER_WECHAT_VOIP) {
            reject_voip_request(pending_voip_reject);
        }
        if (xiaotai_room_active(&s_room)) room_render();
        else if (active_owner == XIAOTAI_OWNER_AI) {
            xiaotai_ui_show_ai(s_ai_view.phase, s_ai_view.emotion,
                               s_ai_view.caption,
                               s_ai_view.caption_type == 1);
        } else if (active_owner == XIAOTAI_OWNER_STREAM) {
            xiaotai_ui_show_status("REMOTE");
        } else {
            xiaotai_ui_show_status("READY");
        }
        BK_LOGW(TAG, "pending incoming call timeout owner=%d\n",
                (int)pending_owner);
    }

    xiaotai_session_owner_t owner = XIAOTAI_OWNER_NONE;
    xiaotai_runtime_state_t state = XIAOTAI_STATE_WAITING;
    uint32_t generation = 0;
    char room_id[sizeof(s_calls.device.room_id)];
    char voip_call_id[sizeof(s_calls.voip.call_id)];
    char *voip_reject_request = NULL;
    bool call_outbound = false;
    room_id[0] = '\0';
    voip_call_id[0] = '\0';

    rtos_lock_mutex(&s_runtime_mutex);
    bool expired = xiaotai_runtime_expire(&s_runtime, rtos_get_time(),
                                          &owner, &state, &generation);
    if (expired && owner == XIAOTAI_OWNER_DEVICE_CALL) {
        snprintf(room_id, sizeof(room_id), "%s", s_calls.device.room_id);
        call_outbound = s_calls.device.outbound;
        xiaotai_call_state_clear_device(&s_calls);
    } else if (expired && owner == XIAOTAI_OWNER_WECHAT_VOIP) {
        if (s_calls.voip.outbound) {
            snprintf(voip_call_id, sizeof(voip_call_id), "%s", s_calls.voip.call_id);
        } else if (state == XIAOTAI_STATE_CALL_INCOMING) {
            voip_reject_request = s_voip_reject_request;
            s_voip_reject_request = NULL;
        }
        clear_voip_fields();
    }
    rtos_unlock_mutex(&s_runtime_mutex);
    if (!expired) return;

    if (state == XIAOTAI_STATE_CALL_ENDING) {
        int disconnect_rc = xiaotai_tirtc_disconnect();
        s_ui_page = UI_PAGE_HOME;
        xiaotai_audio_set_microphone_muted(s_settings.microphone_muted);
        xiaotai_ui_show_status("READY");
        BK_LOGW(TAG,
                "call ending timeout forced local completion owner=%d generation=%u disconnect-rc=%d\n",
                (int)owner, (unsigned)generation, disconnect_rc);
        return;
    }

    bool connected = xiaotai_tirtc_connected();
    if (state != XIAOTAI_STATE_CALL_INCOMING) {
        if (owner == XIAOTAI_OWNER_DEVICE_CALL && room_id[0] != '\0') {
            char request[224];
            bool cancel = !connected && call_outbound;
            int length = xiaotai_call_encode_device_end(
                request, sizeof(request), room_id, "timeout", cancel);
            if (length > 0 && (size_t)length < sizeof(request)) {
                (void)xiaotai_platform_service_request(
                    cancel ? "/v1/call/cancel" : "/v1/call/hangup",
                    request, service_response_ignored, NULL);
            }
        }
        (void)xiaotai_tirtc_disconnect();
    } else if (owner == XIAOTAI_OWNER_DEVICE_CALL && room_id[0] != '\0') {
        reject_device_call(room_id, "timeout");
    } else if (owner == XIAOTAI_OWNER_WECHAT_VOIP) {
        reject_voip_request(voip_reject_request);
    }
    if (voip_call_id[0] != '\0') {
        xiaotai_call_state_mark_voip_stale(
            &s_calls, voip_call_id, rtos_get_time() + 60000U);
    }
    s_home_defer_until_ms = rtos_get_time() + 5000U;
    xiaotai_ui_show_status("TIMEOUT");
    BK_LOGW(TAG, "session timeout owner=%d state=%d generation=%u\n",
            (int)owner, (int)state, (unsigned)generation);
}

static void refresh_idle_home(void)
{
    uint32_t now_ms = rtos_get_time();
    if (s_ui_page == UI_PAGE_WECHAT_QR) {
        /* Match the ESP32 overlay behavior: a QR prompt is an intentional
         * interaction and must not be replaced by the home clock or sleep.
         * MQTT callers_update is the sole contact-change trigger; its handler
         * refreshes contacts and resumes the pending quick call. */
        s_last_activity_ms = now_ms;
        return;
    }
    uint8_t timeout_index = s_settings.screen_timeout_index;
    if (timeout_index >= sizeof(s_screen_timeout_ms) /
                         sizeof(s_screen_timeout_ms[0])) timeout_index = 1U;
    uint32_t timeout_ms = s_screen_timeout_ms[timeout_index];
    rtos_lock_mutex(&s_runtime_mutex);
    bool idle = xiaotai_runtime_home_allowed(&s_runtime);
    rtos_unlock_mutex(&s_runtime_mutex);
    if (idle && timeout_ms != 0U &&
        now_ms - s_last_activity_ms >= timeout_ms) {
        if (xiaotai_ui_backlight_on()) (void)xiaotai_ui_set_backlight(false);
        return;
    }
    if ((int32_t)(now_ms - s_home_defer_until_ms) < 0) return;
    if (s_home_last_refresh_ms != 0U &&
        now_ms - s_home_last_refresh_ms < 2000U) return;
    if (!idle || s_ui_page != UI_PAGE_HOME || !xiaotai_tirtc_ready() ||
        !xiaotai_network_ready()) return;

    wifi_link_status_t link = {0};
    int rssi = bk_wifi_sta_get_link_status(&link) == BK_OK ?
               link.rssi : -128;
    time_t local = xiaotai_time_now() + 8 * 60 * 60;
    struct tm calendar = {0};
    if (gmtime_r(&local, &calendar) == NULL) return;
    char time_text[6];
    char date_text[11];
    snprintf(time_text, sizeof(time_text), "%02d:%02d",
             calendar.tm_hour, calendar.tm_min);
    snprintf(date_text, sizeof(date_text), "%04d-%02d-%02d",
             calendar.tm_year + 1900, calendar.tm_mon + 1, calendar.tm_mday);
    xiaotai_ui_show_home(time_text, date_text, rssi, s_clock_face,
                         s_ai_view.emotion,
                         XIAOTAI_BOARD_TOUCH_AVAILABLE != 0);
    s_home_last_refresh_ms = now_ms;
}

static xiaotai_tirtc_recovery_sdk_state_t recovery_sdk_state(void)
{
    switch (xiaotai_tirtc_state()) {
    case XIAOTAI_TIRTC_STOPPED:
        return XIAOTAI_TIRTC_RECOVERY_SDK_STOPPED;
    case XIAOTAI_TIRTC_STARTING:
        return XIAOTAI_TIRTC_RECOVERY_SDK_STARTING;
    case XIAOTAI_TIRTC_READY:
        return XIAOTAI_TIRTC_RECOVERY_SDK_READY;
    case XIAOTAI_TIRTC_STOPPING:
        return XIAOTAI_TIRTC_RECOVERY_SDK_STOPPING;
    default:
        return XIAOTAI_TIRTC_RECOVERY_SDK_FAILED;
    }
}

static void reboot_after_recovery_failure(const char *reason, int rc)
{
    BK_LOGE(TAG, "TiRTC recovery failed reason=%s rc=%d; rebooting\n",
            reason, rc);
    xiaotai_ui_show_status("RECOVERING");
    rtos_delay_milliseconds(300);
    bk_reboot();
}

static void service_tirtc_recovery(void)
{
    bool requested = xiaotai_tirtc_recovery_required();
    xiaotai_tirtc_recovery_action_t action =
        xiaotai_tirtc_recovery_step(&s_tirtc_recovery, requested,
                                    recovery_sdk_state(), rtos_get_time());
    if (action == XIAOTAI_TIRTC_RECOVERY_NONE) return;
    if (action == XIAOTAI_TIRTC_RECOVERY_STOP) {
        BK_LOGW(TAG,
                "recycling poisoned TiRTC runtime after failed connection cleanup\n");
        xiaotai_ui_show_status("RECOVERING");
        int rc = xiaotai_tirtc_stop();
        if (rc != 0) {
            BK_LOGE(TAG, "TiRTC recovery stop submission failed rc=%d\n", rc);
        }
        return;
    }
    if (action == XIAOTAI_TIRTC_RECOVERY_RESTART) {
        if (!s_tirtc_config_ready) {
            reboot_after_recovery_failure("configuration-unavailable", BK_FAIL);
            return;
        }
        int rc = xiaotai_tirtc_finalize_stop();
        if (rc == 0) rc = xiaotai_tirtc_start(&s_tirtc_config);
        if (rc != 0) {
            reboot_after_recovery_failure("restart-submission", rc);
            return;
        }
        BK_LOGI(TAG, "TiRTC runtime restart submitted\n");
        return;
    }
    if (action == XIAOTAI_TIRTC_RECOVERY_COMPLETE) {
        if (!xiaotai_tirtc_recovery_resources_restored()) {
            reboot_after_recovery_failure("heap-not-restored", BK_FAIL);
            return;
        }
        BK_LOGI(TAG, "TiRTC runtime recovery complete\n");
        rtos_lock_mutex(&s_runtime_mutex);
        bool idle = s_runtime.owner == XIAOTAI_OWNER_NONE;
        rtos_unlock_mutex(&s_runtime_mutex);
        if (idle) {
            xiaotai_ui_show_status("READY");
            refresh_contacts();
        }
        return;
    }
    reboot_after_recovery_failure("lifecycle-timeout", BK_FAIL);
}

static void service_after_room_pending(void)
{
    if (!s_after_room_pending || !s_after_room_transport_closed ||
        !xiaotai_tirtc_transport_quiet_elapsed(
            s_after_room_closed_ms, rtos_get_time(),
            TIRTC_TRANSPORT_QUIET_MS)) {
        return;
    }
    if (rtos_push_to_queue(&s_control_queue, &s_after_room_event, 0) != BK_OK) {
        return;
    }
    memset(&s_after_room_event, 0, sizeof(s_after_room_event));
    s_after_room_pending = false;
    s_after_room_transport_closed = false;
    s_after_room_closed_ms = 0U;
}

static void service_ai_end_drain(void)
{
    rtos_lock_mutex(&s_runtime_mutex);
    if (!s_ai_end_drain.pending) {
        rtos_unlock_mutex(&s_runtime_mutex);
        return;
    }
    bool active = s_runtime.owner == XIAOTAI_OWNER_AI;
    uint32_t generation = s_runtime.generation;
    if (!active) {
        xiaotai_ai_end_drain_cancel(&s_ai_end_drain);
        rtos_unlock_mutex(&s_runtime_mutex);
        return;
    }
    rtos_unlock_mutex(&s_runtime_mutex);
    uint32_t now_ms = rtos_get_time();
    bool drained = xiaotai_audio_playback_is_drained(
        AI_END_PLAYBACK_QUIET_MS);
    rtos_lock_mutex(&s_runtime_mutex);
    if (!s_ai_end_drain.pending) {
        rtos_unlock_mutex(&s_runtime_mutex);
        return;
    }
    bool timed_out = (int32_t)(now_ms - s_ai_end_drain.deadline_ms) >= 0;
    xiaotai_ai_end_drain_result_t result = xiaotai_ai_end_drain_step(
        &s_ai_end_drain, generation, now_ms, drained);
    if (result == XIAOTAI_AI_END_DRAIN_STALE) {
        xiaotai_ai_end_drain_cancel(&s_ai_end_drain);
        rtos_unlock_mutex(&s_runtime_mutex);
        return;
    }
    rtos_unlock_mutex(&s_runtime_mutex);
    if (result != XIAOTAI_AI_END_DRAIN_DISCONNECT) return;
    BK_LOGI(TAG,
            "AI final playback %s; disconnecting generation=%u\n",
            drained ? "drained" : (timed_out ? "drain timeout" : "complete"),
            (unsigned)generation);
    (void)xiaotai_tirtc_disconnect();
}

static void control_maintenance(void)
{
    xiaotai_ui_refresh_verification_countdown();
    service_room_touch_release();
    if (s_ui_page == UI_PAGE_DIAGNOSTICS &&
        (int32_t)(rtos_get_time() - s_diagnostics_due_ms) >= 0)
        render_diagnostics();
    service_tirtc_recovery();
    service_after_room_pending();
    service_ai_end_drain();
    if (s_stream_media_diagnostic_due_ms != 0U &&
        (int32_t)(rtos_get_time() - s_stream_media_diagnostic_due_ms) >= 0) {
        s_stream_media_diagnostic_due_ms = 0U;
        xiaotai_tirtc_log_stream_media_state();
    }
    handle_runtime_timeout();
    xiaotai_room_tick(&s_room);
    refresh_idle_home();
}

static void control_task(beken_thread_arg_t argument)
{
    (void)argument;
    control_event_t event;
    for (;;) {
        service_room_touch_release();
        if (rtos_pop_from_queue(&s_control_queue, &event,
                                CONTROL_POLL_MS) != BK_OK) {
            control_maintenance();
            continue;
        }
        if (event.type == CONTROL_BUTTON) {
            handle_intent((xiaotai_intent_t)event.input_event);
        }
        else if (event.type == CONTROL_AI_TOKEN) handle_ai_token(&event);
        else if (event.type == CONTROL_AI_CONNECTED) {
            handle_ai_connected(event.generation);
        }
        else if (event.type == CONTROL_PLATFORM_SIGNAL) {
            handle_platform_signal(&event);
        } else if (event.type == CONTROL_CALL_INFO) {
            handle_call_info(&event);
        } else if (event.type == CONTROL_CONTACTS) {
            handle_contacts(&event);
        } else if (event.type == CONTROL_AI_CALL_INTENT) {
            handle_ai_call_intent(&event);
        } else if (event.type == CONTROL_AI_UI) {
            handle_ai_ui(&event);
        } else if (event.type == CONTROL_AI_END) {
            handle_ai_end(event.generation);
        } else if (event.type == CONTROL_OUTBOUND_CALL) {
            handle_outbound_call(&event);
        } else if (event.type == CONTROL_DIAL_CONTACT) {
            handle_dial_contact();
        } else if (event.type == CONTROL_TOUCH) {
            handle_touch(&event);
        } else if (event.type == CONTROL_ROOM_ASSIGNMENT) {
            handle_room_assignment(&event);
        } else if (event.type == CONTROL_ROOM_TOKEN) {
            handle_room_token(&event);
        } else if (event.type == CONTROL_ROOM_PRESENCE) {
            handle_room_presence(&event);
        } else if (event.type == CONTROL_ROOM_CONNECTED) {
            handle_room_connected(event.generation);
        } else if (event.type == CONTROL_ROOM_COMMAND) {
            handle_room_command(&event);
        } else if (event.type == CONTROL_ROOM_DISCONNECTED) {
            handle_room_disconnected(event.generation, event.error);
        } else if (event.type == CONTROL_CONSOLE_CALL) {
            handle_console_call(&event);
        } else if (event.type == CONTROL_CONSOLE_CALL_ACTION) {
            handle_console_call_action(&event);
        } else if (event.type == CONTROL_CONSOLE_ROOM) {
            handle_console_room(&event);
        } else if (event.type == CONTROL_ACCEPT_PENDING) {
            accept_pending_call();
        }
        os_free(event.text);
        control_maintenance();
    }
}

static bool on_stream_connected(uint32_t generation, void *context)
{
    xiaotai_runtime_t *runtime = context;
    rtos_lock_mutex(&s_runtime_mutex);
    bool admitted = xiaotai_runtime_remote_view_allowed(
        runtime, xiaotai_room_active(&s_room));
    bool accepted = admitted &&
        xiaotai_runtime_begin(runtime, XIAOTAI_OWNER_STREAM, true);
    if (accepted) {
        s_stream_transport_generation = generation;
        s_stream_runtime_generation = runtime->generation;
    }
    rtos_unlock_mutex(&s_runtime_mutex);
    if (!accepted) {
        BK_LOGW(TAG, "STREAM rejected: product media path is busy\n");
        return false;
    }
    BK_LOGI(TAG, "remote STREAM connected generation=%u\n",
            (unsigned)generation);
    s_stream_media_diagnostic_due_ms =
        rtos_get_time() + STREAM_MEDIA_DIAGNOSTIC_MS;
    xiaotai_ui_show_status("REMOTE");
    return true;
}

static void on_stream_disconnected(uint32_t generation,
                                   int error,
                                   void *context)
{
    xiaotai_runtime_t *runtime = context;
    rtos_lock_mutex(&s_runtime_mutex);
    bool finished = generation == s_stream_transport_generation &&
                    runtime->owner == XIAOTAI_OWNER_STREAM &&
                    xiaotai_runtime_finish(runtime,
                                           s_stream_runtime_generation);
    if (finished) {
        s_stream_transport_generation = 0;
        s_stream_runtime_generation = 0;
        s_stream_media_diagnostic_due_ms = 0U;
    }
    rtos_unlock_mutex(&s_runtime_mutex);
    if (!finished) {
        BK_LOGW(TAG, "stale STREAM close transport=%u runtime=%u\n",
                (unsigned)generation, (unsigned)runtime->generation);
        return;
    }
    BK_LOGI(TAG, "remote STREAM disconnected generation=%u error=%d\n",
            (unsigned)generation, error);
    s_ui_page = UI_PAGE_HOME;
    bool normal_close = error == 0 || error == TIRTC_E_CONN_REMOTECLOSE;
    if (!show_pending_incoming()) {
        xiaotai_ui_show_status(normal_close ? "READY" : "ERROR");
    }
    rtos_lock_mutex(&s_runtime_mutex);
    bool accept_pending = s_accept_pending_after_media &&
                          xiaotai_runtime_has_incoming(&s_runtime);
    if (accept_pending) s_accept_pending_after_media = false;
    rtos_unlock_mutex(&s_runtime_mutex);
    if (accept_pending) queue_accept_pending();
}

static void on_room_connected(uint32_t generation, void *context)
{
    (void)context;
    control_event_t event = {
        .type = CONTROL_ROOM_CONNECTED,
        .generation = generation,
    };
    if (rtos_push_to_queue(&s_control_queue, &event, 0) != BK_OK) {
        BK_LOGE(TAG, "room connected event dropped\n");
        (void)xiaotai_tirtc_disconnect();
    }
}

static void on_room_command(uint32_t generation, uint32_t command,
                            const void *data, uint32_t length, void *context)
{
    (void)context;
    if (((command & 0xffffU) & ~RESPONSE_BIT) != XIAOTAI_ROOM_COMMAND ||
        data == NULL || length == 0U) return;
    queue_text_event(CONTROL_ROOM_COMMAND, generation,
                     (const char *)data, length);
}

static void on_room_disconnected(uint32_t generation, int error, void *context)
{
    (void)context;
    control_event_t event = {
        .type = CONTROL_ROOM_DISCONNECTED,
        .generation = generation,
        .error = error,
    };
    if (rtos_push_to_queue(&s_control_queue, &event, 0) != BK_OK) {
        BK_LOGE(TAG, "room disconnected event dropped\n");
    }
}

static void handle_ai_connected(uint32_t generation)
{
    xiaotai_runtime_t *runtime = &s_runtime;
    rtos_lock_mutex(&s_runtime_mutex);
    bool current = runtime->owner == XIAOTAI_OWNER_AI &&
                   runtime->generation == generation &&
                   xiaotai_runtime_connected(runtime, generation);
    if (current) {
        arm_runtime_timeout_locked(generation, SESSION_CONNECT_TIMEOUT_MS);
    }
    rtos_unlock_mutex(&s_runtime_mutex);
    if (!current) {
        (void)xiaotai_tirtc_disconnect();
        return;
    }

    /* Match the ESP32 product sequence: make playback and Opus ready before
     * start_session, since the server may publish its first response audio
     * immediately. Keep uplink gated until START_ACCEPTED below. */
    int rc = xiaotai_tirtc_ai_prepare_media();
    if (rc < 0) {
        BK_LOGE(TAG, "AI media prewarm failed rc=%d\n", rc);
        (void)xiaotai_tirtc_disconnect();
        return;
    }
    BK_LOGI(TAG, "AI media prewarmed before start_session\n");

    /* Let the KCP/media path settle outside the TiRTC callback. */
    rtos_delay_milliseconds(300);
    rtos_lock_mutex(&s_runtime_mutex);
    current = runtime->owner == XIAOTAI_OWNER_AI &&
              runtime->generation == generation &&
              xiaotai_runtime_connected(runtime, generation);
    rtos_unlock_mutex(&s_runtime_mutex);
    if (!current) return;

    snprintf(s_ai_request_id, sizeof(s_ai_request_id), "%08x%08x",
             (unsigned)generation, (unsigned)rtos_get_time());
    char request[512];
    int size = xiaotai_ai_encode_start(request, sizeof(request),
                                       s_ai_request_id, s_device_id,
                                       s_ai_role_id);
    rc = size > 0 && (size_t)size < sizeof(request) ?
        xiaotai_tirtc_send_command(XIAOTAI_AI_COMMAND, request,
                                   (uint32_t)size) :
        BK_FAIL;
    if (rc < 0) {
        BK_LOGE(TAG, "AI start_session send failed rc=%d\n", rc);
        (void)xiaotai_tirtc_disconnect();
    } else {
        BK_LOGI(TAG, "AI start_session sent; microphone uplink remains gated\n");
    }
}

static void on_ai_connected(uint32_t generation, void *context)
{
    (void)context;
    control_event_t event = {
        .type = CONTROL_AI_CONNECTED,
        .generation = generation,
    };
    if (rtos_push_to_queue(&s_control_queue, &event, 0) != BK_OK) {
        BK_LOGE(TAG, "AI connected event dropped generation=%u\n",
                (unsigned)generation);
    }
}

static void on_ai_command(uint32_t generation, uint32_t command,
                          const void *data, uint32_t length, void *context)
{
    xiaotai_runtime_t *runtime = context;
    if (!xiaotai_ai_accepts_command(command, RESPONSE_BIT) ||
        data == NULL || length == 0U) return;
    xiaotai_ai_message_t message = xiaotai_ai_decode_message(
        (const char *)data, length, s_ai_request_id);
    if (message == XIAOTAI_AI_MESSAGE_CALL_INTENT) {
        queue_text_event(CONTROL_AI_CALL_INTENT, generation,
                         (const char *)data, length);
        return;
    }
    if (message == XIAOTAI_AI_MESSAGE_UI) {
        if (length <= 2048U) {
            queue_text_event(CONTROL_AI_UI, generation,
                             (const char *)data, length);
        } else {
            BK_LOGW(TAG, "AI UI event too large bytes=%u\n",
                    (unsigned)length);
        }
        return;
    }
    if (message == XIAOTAI_AI_MESSAGE_END) {
        BK_LOGI(TAG, "AI end_session command received generation=%u bytes=%u\n",
                (unsigned)generation, (unsigned)length);
        control_event_t event = {
            .type = CONTROL_AI_END,
            .generation = generation,
        };
        if (rtos_push_to_queue(&s_control_queue, &event, 0) != BK_OK) {
            BK_LOGE(TAG, "AI end_session event dropped generation=%u\n",
                    (unsigned)generation);
        }
        return;
    }
    if (message != XIAOTAI_AI_MESSAGE_START_ACCEPTED) {
        if (message == XIAOTAI_AI_MESSAGE_START_REJECTED) {
            BK_LOGE(TAG, "AI start_session rejected or profile unsupported\n");
            (void)xiaotai_tirtc_disconnect();
        }
        return;
    }

    int rc = xiaotai_tirtc_ai_start_media();
    rtos_lock_mutex(&s_runtime_mutex);
    bool active = rc == BK_OK && runtime->owner == XIAOTAI_OWNER_AI &&
                  xiaotai_runtime_media_started(runtime, generation);
    rtos_unlock_mutex(&s_runtime_mutex);
    if (!active) {
        BK_LOGE(TAG, "AI audio start failed rc=%d\n", rc);
        (void)xiaotai_tirtc_disconnect();
        return;
    }
    xiaotai_ai_view_init(&s_ai_view);
    xiaotai_ui_show_ai(s_ai_view.phase, s_ai_view.emotion,
                       s_ai_view.caption, false);
    BK_LOGI(TAG, "AI talk active generation=%u\n", (unsigned)generation);
}

static void on_ai_disconnected(uint32_t generation, int error, void *context)
{
    (void)context;
    rtos_lock_mutex(&s_runtime_mutex);
    bool should_dial = s_pending_dial_generation == generation &&
                       s_runtime.owner == XIAOTAI_OWNER_AI &&
                       s_runtime.generation == generation;
    rtos_unlock_mutex(&s_runtime_mutex);
    finish_ai(generation, error);
    rtos_lock_mutex(&s_runtime_mutex);
    bool accept_pending = s_accept_pending_after_media &&
                          xiaotai_runtime_has_incoming(&s_runtime);
    if (accept_pending) s_accept_pending_after_media = false;
    rtos_unlock_mutex(&s_runtime_mutex);
    if (accept_pending) queue_accept_pending();
    if (should_dial) {
        control_event_t event = {.type = CONTROL_DIAL_CONTACT};
        if (rtos_push_to_queue(&s_control_queue, &event, 0) != BK_OK) {
            rtos_lock_mutex(&s_runtime_mutex);
            s_pending_dial_generation = 0U;
            memset(&s_pending_contact, 0, sizeof(s_pending_contact));
            rtos_unlock_mutex(&s_runtime_mutex);
            BK_LOGE(TAG, "post-AI dial event dropped\n");
        }
    }
}

static void on_call_connected(uint32_t generation, void *context)
{
    xiaotai_runtime_t *runtime = context;
    rtos_lock_mutex(&s_runtime_mutex);
    bool active = runtime->owner == XIAOTAI_OWNER_DEVICE_CALL &&
                  runtime->generation == generation &&
                  xiaotai_runtime_media_started(runtime, generation);
    rtos_unlock_mutex(&s_runtime_mutex);
    if (!active) {
        (void)xiaotai_tirtc_disconnect();
        return;
    }
    s_call_microphone_muted = s_settings.microphone_muted;
    xiaotai_audio_set_microphone_muted(s_call_microphone_muted);
    xiaotai_ui_show_call_active(false, s_call_microphone_muted);
    BK_LOGI(TAG, "device call media active generation=%u type=%s\n",
            (unsigned)generation, s_calls.device.type);
}

static void on_call_disconnected(uint32_t generation, int error, void *context)
{
    (void)context;
    finish_call(generation, error);
}

static void on_voip_connected(uint32_t generation, void *context)
{
    xiaotai_runtime_t *runtime = context;
    rtos_lock_mutex(&s_runtime_mutex);
    bool current = runtime->owner == XIAOTAI_OWNER_WECHAT_VOIP &&
                   runtime->generation == generation &&
                   runtime->state == XIAOTAI_STATE_CALL_CONNECTING;
    if (current) {
        arm_runtime_timeout_locked(generation, SESSION_CONNECT_TIMEOUT_MS);
    }
    rtos_unlock_mutex(&s_runtime_mutex);
    if (!current) {
        (void)xiaotai_tirtc_disconnect();
        return;
    }
    xiaotai_ui_show_status("VOIP CONNECT");
    BK_LOGI(TAG, "VoIP transport connected; waiting platform accept\n");
}

static void on_voip_active(uint32_t generation, void *context)
{
    xiaotai_runtime_t *runtime = context;
    rtos_lock_mutex(&s_runtime_mutex);
    bool active = runtime->owner == XIAOTAI_OWNER_WECHAT_VOIP &&
                  runtime->generation == generation &&
                  xiaotai_runtime_media_started(runtime, generation);
    rtos_unlock_mutex(&s_runtime_mutex);
    if (!active) {
        (void)xiaotai_tirtc_disconnect();
        return;
    }
    s_call_microphone_muted = s_settings.microphone_muted;
    xiaotai_audio_set_microphone_muted(s_call_microphone_muted);
    xiaotai_ui_show_call_active(true, s_call_microphone_muted);
    BK_LOGI(TAG, "VoIP audio active generation=%u\n", (unsigned)generation);
}

static void on_voip_disconnected(uint32_t generation, int error, void *context)
{
    (void)context;
    finish_voip(generation, error);
}

static void on_platform_signal(const char *json, size_t size, void *context)
{
    (void)context;
    queue_text_event(CONTROL_PLATFORM_SIGNAL, 0, json, size);
}

static void supervisor_task(beken_thread_arg_t argument)
{
    (void)argument;
    while (!xiaotai_network_ready()) rtos_delay_milliseconds(200);

    xiaotai_device_credentials_t credentials = {0};
    while (xiaotai_storage_load_device(&credentials) != BK_OK) {
        BK_LOGI(TAG, "device is unbound; starting verification binding\n");
        if (xiaotai_platform_bind(&credentials) == BK_OK &&
            xiaotai_storage_save_device(&credentials) == BK_OK) {
            xiaotai_ui_show_status("BOUND");
            BK_LOGI(TAG, "device credentials persisted\n");
            break;
        }
        memset(&credentials, 0, sizeof(credentials));
        BK_LOGW(TAG, "binding attempt ended; retrying in 5 seconds\n");
        rtos_delay_milliseconds(5000);
        while (!xiaotai_network_ready()) rtos_delay_milliseconds(200);
    }
    snprintf(s_device_id, sizeof(s_device_id), "%s", credentials.device_id);
    xiaotai_room_set_device_id(&s_room, s_device_id);

    /* TiRTC signs its synchronous /v1/start request with the wall clock.
     * Platform capability reporting performs service discovery and SNTP
     * synchronization, so it must complete before TiRtcStart.  Starting in
     * the opposite order leaves the RTC at the Unix epoch and the SDK's
     * service request fails with its generic -1 result. */
    for (;;) {
        int platform_rc =
            xiaotai_platform_report_capabilities(&credentials);
        if (platform_rc == BK_OK) break;
        BK_LOGW(TAG,
                "platform preparation failed rc=%d; retrying in 5 seconds\n",
                platform_rc);
        rtos_delay_milliseconds(5000);
        while (!xiaotai_network_ready()) rtos_delay_milliseconds(200);
    }

    char tirtc_endpoint[256];
    if (xiaotai_platform_tirtc_endpoint(tirtc_endpoint,
                                        sizeof(tirtc_endpoint)) != BK_OK) {
        BK_LOGE(TAG, "TiRTC control endpoint unavailable\n");
        rtos_delete_thread(NULL);
        return;
    }
    char tirtc_client_id[18];
    if (xiaotai_platform_client_id(tirtc_client_id,
                                   sizeof(tirtc_client_id)) != BK_OK) {
        BK_LOGE(TAG, "hardware client id unavailable\n");
        rtos_delete_thread(NULL);
        return;
    }
    snprintf(s_tirtc_device_id, sizeof(s_tirtc_device_id), "%s",
             credentials.device_id);
    snprintf(s_tirtc_device_secret, sizeof(s_tirtc_device_secret), "%s",
             credentials.device_secret);
    snprintf(s_tirtc_client_id, sizeof(s_tirtc_client_id), "%s",
             tirtc_client_id);
    snprintf(s_tirtc_endpoint, sizeof(s_tirtc_endpoint), "%s",
             tirtc_endpoint);
    s_tirtc_config = (xiaotai_tirtc_config_t){
        .device_id = s_tirtc_device_id,
        .device_secret = s_tirtc_device_secret,
        /* Match device-sim-c: CLIENT_ID is the stable hardware MAC while
         * device_id remains the cloud-issued TiRTC identity. */
        .client_id = s_tirtc_client_id,
        .service_endpoint = s_tirtc_endpoint,
        /* The BK7258 mini SDK shares PSRAM with DTLS and the camera.
         * 256 KiB left too little contiguous memory for a second WHIP
         * handshake (mbedtls_ssl_setup returned MBEDTLS_ERR_SSL_ALLOC_FAILED).
         * One 720p H264 frame is capped at 100 KiB, so 128 KiB retains one
         * complete frame plus transport overhead while restoring 128 KiB to
         * the shared heap. */
        .max_send_buffer = 128U * 1024U,
        /* The product arbiter owns one media session at a time. This matches
         * the TiRTC ESP32 reference and avoids reserving an unused slot. */
        .max_connections = 1,
        .poll_timeout_ms = 20,
    };
    s_tirtc_config_ready = true;
    BK_LOGI(TAG, "TiRTC identity configured device-id-len=%u client-id=base-mac\n",
            (unsigned)strlen(credentials.device_id));
    int rc;
    for (;;) {
        BK_LOGI(TAG, "TiRTC bootstrap starting stack=%u bytes\n",
                (unsigned)TIRTC_SUPERVISOR_STACK_SIZE);
        rc = xiaotai_tirtc_start(&s_tirtc_config);
        if (rc == 0) break;
        BK_LOGW(TAG, "TiRTC bootstrap failed rc=%d; retrying in 5 seconds\n",
                rc);
        rtos_delay_milliseconds(5000);
        while (!xiaotai_network_ready()) rtos_delay_milliseconds(200);
    }
    BK_LOGI(TAG, "TiRTC bootstrap submitted\n");
    xiaotai_ui_show_status("READY");
    for (unsigned wait = 0; wait < 100U && !xiaotai_tirtc_ready(); ++wait) {
        rtos_delay_milliseconds(50);
    }
    if (xiaotai_tirtc_ready()) refresh_contacts();
    else BK_LOGW(TAG, "initial contact refresh deferred: TiRTC not ready\n");
    (void)xiaotai_platform_run(&credentials, on_platform_signal, &s_runtime);
    memset(&credentials, 0, sizeof(credentials));
    BK_LOGE(TAG, "platform loop stopped unexpectedly\n");
    rtos_delete_thread(NULL);
}

int xiaotai_app_start(void)
{
    xiaotai_runtime_init(&s_runtime);
    xiaotai_call_state_init(&s_calls);
    xiaotai_tirtc_recovery_init(&s_tirtc_recovery);
    xiaotai_ai_view_init(&s_ai_view);
    xiaotai_contacts_init(&s_contacts);
    xiaotai_product_settings_default(&s_settings);
    (void)xiaotai_storage_load_settings(&s_settings);
    xiaotai_audio_set_volume(s_settings.volume);
    xiaotai_audio_set_speaker_muted(s_settings.speaker_muted);
    xiaotai_audio_set_microphone_muted(s_settings.microphone_muted);
    (void)xiaotai_audio_set_microphone_sensitivity(
        s_settings.microphone_sensitivity);
    s_last_activity_ms = rtos_get_time();
    int rc = rtos_init_mutex(&s_runtime_mutex);
    if (rc != BK_OK) return rc;
    rc = rtos_init_mutex(&s_room_snapshot_mutex);
    if (rc != BK_OK) return rc;
    room_init(NULL);
    rc = rtos_init_queue(&s_control_queue, "xiaotai_control",
                         sizeof(control_event_t), CONTROL_QUEUE_DEPTH);
    if (rc != BK_OK) return rc;
    rc = rtos_create_psram_thread(&s_control_thread, 5, "xiaotai_control",
                                  control_task, CONTROL_TASK_STACK_SIZE, NULL);
    if (rc != BK_OK) return rc;
    const xiaotai_tirtc_handlers_t tirtc_handlers = {
        .on_stream_connected = on_stream_connected,
        .on_stream_disconnected = on_stream_disconnected,
        .on_ai_connected = on_ai_connected,
        .on_ai_command = on_ai_command,
        .on_ai_disconnected = on_ai_disconnected,
        .on_call_connected = on_call_connected,
        .on_call_disconnected = on_call_disconnected,
        .on_voip_connected = on_voip_connected,
        .on_voip_active = on_voip_active,
        .on_voip_disconnected = on_voip_disconnected,
        .on_room_connected = on_room_connected,
        .on_room_command = on_room_command,
        .on_room_disconnected = on_room_disconnected,
        .context = &s_runtime,
    };
    xiaotai_tirtc_set_handlers(&tirtc_handlers);
    BK_LOGI(TAG, "XiaoTai BK7258 product runtime initialized\n");
    BK_LOGI(TAG, "TiRTC package linked: %s\n", xiaotai_tirtc_version());
    rc = xiaotai_button_start(button_pressed, NULL);
    if (rc != BK_OK) {
        BK_LOGE(TAG, "AI button initialization failed rc=%d\n", rc);
        return rc;
    }
    BK_LOGI(TAG,
            "KEY map: short=answer/hangup/AI, double=contacts[0], long=room PTT\n");
#if XIAOTAI_BOARD_TOUCH_AVAILABLE
    rc = xiaotai_touch_start(touch_event, NULL);
    if (rc != BK_OK) {
        BK_LOGW(TAG, "touch initialization failed rc=%d\n", rc);
    }
#else
    BK_LOGI(TAG, "board profile disables touch\n");
#endif
    rc = xiaotai_network_start();
    if (rc != 0) {
        BK_LOGE(TAG, "network bootstrap failed rc=%d\n", rc);
        return rc;
    }
    BK_LOGI(TAG, "network bootstrap started; binding begins after STA obtains IP\n");
    rc = rtos_create_psram_thread(NULL, BEKEN_APPLICATION_PRIORITY,
                                  "xiaotai_supervisor", supervisor_task,
                                  TIRTC_SUPERVISOR_STACK_SIZE, NULL);
    return rc;
}

static int queue_console_intent(xiaotai_session_owner_t required_owner,
                                bool require_idle)
{
    if (s_control_queue == NULL || s_runtime_mutex == NULL) {
        return BK_ERR_NOT_INIT;
    }
    rtos_lock_mutex(&s_runtime_mutex);
    bool allowed = require_idle ?
                   (s_runtime.owner == XIAOTAI_OWNER_NONE &&
                    !xiaotai_runtime_has_incoming(&s_runtime)) :
                   s_runtime.owner == required_owner;
    rtos_unlock_mutex(&s_runtime_mutex);
    if (!allowed) return BK_ERR_BUSY;
    control_event_t event = {
        .type = CONTROL_BUTTON,
        .input_event = XIAOTAI_INTENT_PRIMARY,
    };
    return rtos_push_to_queue(&s_control_queue, &event, 0);
}

int xiaotai_app_request_ai_start(void)
{
    return queue_console_intent(XIAOTAI_OWNER_NONE, true);
}

int xiaotai_app_request_ai_stop(void)
{
    return queue_console_intent(XIAOTAI_OWNER_AI, false);
}

int xiaotai_app_runtime_snapshot(xiaotai_runtime_t *out)
{
    if (out == NULL) return BK_ERR_PARAM;
    if (s_runtime_mutex == NULL) return BK_ERR_NOT_INIT;
    rtos_lock_mutex(&s_runtime_mutex);
    *out = s_runtime;
    rtos_unlock_mutex(&s_runtime_mutex);
    return BK_OK;
}

static int queue_console_event(control_event_type_t type, const char *text,
                               uint32_t action, int value)
{
    if (s_control_queue == NULL) return BK_ERR_NOT_INIT;
    control_event_t event = {
        .type = type,
        .generation = action,
        .error = value,
    };
    if (text != NULL && text[0] != '\0') {
        size_t length = strlen(text);
        if (length > XIAOTAI_CONTACT_NAME_MAX) return BK_ERR_PARAM;
        event.text = os_malloc(length + 1U);
        if (event.text == NULL) return BK_ERR_NO_MEM;
        memcpy(event.text, text, length + 1U);
    }
    int rc = rtos_push_to_queue(&s_control_queue, &event, 0);
    if (rc != BK_OK) os_free(event.text);
    return rc;
}

int xiaotai_app_request_call(const char *contact_name, bool wechat)
{
    if (!wechat && (contact_name == NULL || contact_name[0] == '\0')) {
        return BK_ERR_PARAM;
    }
    return queue_console_event(CONTROL_CONSOLE_CALL, contact_name, 0U,
                               wechat ? XIAOTAI_CONTACT_VOIP :
                                        XIAOTAI_CONTACT_DEVICE);
}

int xiaotai_app_request_call_answer(xiaotai_session_owner_t expected_owner)
{
    if (expected_owner != XIAOTAI_OWNER_NONE &&
        expected_owner != XIAOTAI_OWNER_DEVICE_CALL &&
        expected_owner != XIAOTAI_OWNER_WECHAT_VOIP) return BK_ERR_PARAM;
    return queue_console_event(CONTROL_CONSOLE_CALL_ACTION, NULL,
                               CONSOLE_CALL_ANSWER, (int)expected_owner);
}

int xiaotai_app_request_call_hangup(void)
{
    return queue_console_event(CONTROL_CONSOLE_CALL_ACTION, NULL,
                               CONSOLE_CALL_HANGUP, XIAOTAI_OWNER_NONE);
}

int xiaotai_app_call_snapshot(xiaotai_runtime_t *out, bool *outbound)
{
    if (out == NULL || outbound == NULL) return BK_ERR_PARAM;
    if (s_runtime_mutex == NULL) return BK_ERR_NOT_INIT;
    rtos_lock_mutex(&s_runtime_mutex);
    *out = s_runtime;
    if (out->owner == XIAOTAI_OWNER_DEVICE_CALL) *outbound = s_calls.device.outbound;
    else if (out->owner == XIAOTAI_OWNER_WECHAT_VOIP) {
        *outbound = s_calls.voip.outbound;
    } else {
        *outbound = false;
    }
    rtos_unlock_mutex(&s_runtime_mutex);
    return BK_OK;
}

static bool decimal_string(const char *value, size_t length)
{
    if (value == NULL || strlen(value) != length) return false;
    for (size_t i = 0; i < length; ++i) {
        if (value[i] < '0' || value[i] > '9') return false;
    }
    return true;
}

int xiaotai_app_request_room_join(const char *room_code,
                                  const char *password)
{
    if (!decimal_string(room_code, 6U) ||
        (password != NULL && password[0] != '\0' &&
         !decimal_string(password, 4U))) return BK_ERR_PARAM;
    char request[80];
    int length = password != NULL && password[0] != '\0' ?
        snprintf(request, sizeof(request),
                 "{\"room_code\":\"%s\",\"password\":\"%s\"}",
                 room_code, password) :
        snprintf(request, sizeof(request),
                 "{\"room_code\":\"%s\"}", room_code);
    if (length <= 0 || (size_t)length >= sizeof(request)) return BK_ERR_PARAM;
    return queue_console_event(CONTROL_CONSOLE_ROOM, request,
                               CONSOLE_ROOM_JOIN, 0);
}

int xiaotai_app_request_room_leave(void)
{
    return queue_console_event(CONTROL_CONSOLE_ROOM, NULL,
                               CONSOLE_ROOM_LEAVE, 0);
}

int xiaotai_app_request_room_talk(bool enabled)
{
    return queue_console_event(CONTROL_CONSOLE_ROOM, NULL,
        enabled ? CONSOLE_ROOM_TALK_START : CONSOLE_ROOM_TALK_STOP, 0);
}

int xiaotai_app_request_room_sync(void)
{
    return queue_console_event(CONTROL_CONSOLE_ROOM, NULL,
                               CONSOLE_ROOM_SYNC, 0);
}

int xiaotai_app_room_snapshot(xiaotai_room_snapshot_t *out)
{
    if (out == NULL) return BK_ERR_PARAM;
    if (s_room_snapshot_mutex == NULL) return BK_ERR_NOT_INIT;
    rtos_lock_mutex(&s_room_snapshot_mutex);
    *out = s_room_snapshot;
    rtos_unlock_mutex(&s_room_snapshot_mutex);
    return BK_OK;
}
