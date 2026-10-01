#include "xiaotai_console.h"

#include <common/bk_err.h>
#include <components/system.h>
#include <os/mem.h>
#include <os/os.h>
#include <stdbool.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include "cli.h"
#include "xiaotai_app.h"
#include "xiaotai_log.h"
#include "xiaotai_network.h"
#include "xiaotai_storage.h"
#include "xiaotai_tirtc.h"

static bool s_reboot_pending;

static void response(char *output, int capacity, const char *message)
{
    if (output != NULL && capacity > 0) {
        snprintf(output, (size_t)capacity, "%s", message);
    }
}

static void reboot_task(beken_thread_arg_t argument)
{
    (void)argument;
    rtos_delay_milliseconds(500);
    bk_reboot();
    rtos_delete_thread(NULL);
}

static int schedule_reboot(void)
{
    if (s_reboot_pending) return BK_OK;
    int rc = rtos_create_thread(NULL, BEKEN_APPLICATION_PRIORITY,
                                "xiaotai_reboot", reboot_task, 1024, NULL);
    if (rc == BK_OK) s_reboot_pending = true;
    return rc;
}

static const char *owner_name(xiaotai_session_owner_t owner)
{
    switch (owner) {
    case XIAOTAI_OWNER_STREAM: return "stream";
    case XIAOTAI_OWNER_AI: return "ai";
    case XIAOTAI_OWNER_DEVICE_CALL: return "device-call";
    case XIAOTAI_OWNER_WECHAT_VOIP: return "wechat-voip";
    case XIAOTAI_OWNER_ROOM: return "room";
    default: return "none";
    }
}

static void command_status(char *output, int capacity, int argc, char **argv)
{
    (void)argv;
    if (argc != 1) {
        response(output, capacity, "usage: status\r\n");
        return;
    }
    xiaotai_wifi_credentials_t wifi;
    xiaotai_device_credentials_t device;
    xiaotai_runtime_t runtime = {0};
    bool has_wifi = xiaotai_storage_load_wifi(&wifi) == BK_OK;
    bool bound = xiaotai_storage_load_device(&device) == BK_OK;
    bool has_runtime = xiaotai_app_runtime_snapshot(&runtime) == BK_OK;
    char ip[20] = {0};
    (void)xiaotai_network_get_ip(ip, sizeof(ip));
    if (output == NULL || capacity <= 0) return;
    snprintf(output, (size_t)capacity,
             "wifi=%s saved=%d ap=%d ip=%s bound=%d tirtc=%d connected=%d "
             "owner=%s state=%d pending=%s internal_free=%u internal_min=%u "
             "psram_free=%u psram_min=%u\r\n",
             xiaotai_network_ready() ? "connected" : "offline",
             has_wifi ? 1 : 0, xiaotai_network_provisioning() ? 1 : 0,
             ip[0] == '\0' ? "-" : ip, bound ? 1 : 0,
             (int)xiaotai_tirtc_state(), xiaotai_tirtc_connected() ? 1 : 0,
             has_runtime ? owner_name(runtime.owner) : "not-ready",
             has_runtime ? (int)runtime.state : -1,
             has_runtime ? owner_name(runtime.pending_incoming) : "not-ready",
             (unsigned)rtos_get_free_heap_size(),
             (unsigned)rtos_get_minimum_free_heap_size(),
             (unsigned)rtos_get_psram_free_heap_size(),
             (unsigned)rtos_get_psram_minimum_free_heap_size());
}

static void command_ai_start(char *output, int capacity, int argc, char **argv)
{
    (void)argv;
    int rc = argc == 1 ? xiaotai_app_request_ai_start() : BK_ERR_PARAM;
    response(output, capacity, rc == BK_OK ? "AI start requested\r\n" :
             argc == 1 ? "AI start rejected\r\n" : "usage: ai-start\r\n");
}

static void command_ai_stop(char *output, int capacity, int argc, char **argv)
{
    (void)argv;
    int rc = argc == 1 ? xiaotai_app_request_ai_stop() : BK_ERR_PARAM;
    response(output, capacity, rc == BK_OK ? "AI stop requested\r\n" :
             argc == 1 ? "AI stop rejected\r\n" : "usage: ai-stop\r\n");
}

static void command_wifi_set(char *output, int capacity, int argc, char **argv)
{
    if (argc != 3) {
        response(output, capacity,
                 "usage: wifi-set <ssid> <password>; use \"\" for open Wi-Fi\r\n");
        return;
    }
    xiaotai_wifi_credentials_t value = {0};
    if (strlen(argv[1]) >= sizeof(value.ssid) ||
        strlen(argv[2]) >= sizeof(value.password)) {
        response(output, capacity, "invalid Wi-Fi config: argument too long\r\n");
        return;
    }
    snprintf(value.ssid, sizeof(value.ssid), "%s", argv[1]);
    snprintf(value.password, sizeof(value.password), "%s", argv[2]);
    int rc = xiaotai_storage_save_wifi(&value);
    if (rc == BK_OK) rc = schedule_reboot();
    response(output, capacity, rc == BK_OK ?
             "Wi-Fi config saved; restarting\r\n" :
             "Wi-Fi config save failed\r\n");
}

static void command_wifi_clear(char *output, int capacity,
                               int argc, char **argv)
{
    (void)argv;
    int rc = argc == 1 ? xiaotai_storage_clear_wifi() : BK_ERR_PARAM;
    if (rc == BK_OK) rc = schedule_reboot();
    response(output, capacity, rc == BK_OK ?
             "Wi-Fi config cleared; restarting into SoftAP\r\n" :
             argc == 1 ? "Wi-Fi config clear failed\r\n" :
             "usage: wifi-clear\r\n");
}

static void command_wifi_change(char *output, int capacity,
                                int argc, char **argv)
{
    (void)argv;
    int rc = argc == 1 ? xiaotai_network_enter_provisioning() : BK_ERR_PARAM;
    response(output, capacity, rc == BK_OK ?
             "Wi-Fi disconnected; provisioning opened; credentials retained\r\n" :
             argc == 1 ? "Wi-Fi change rejected\r\n" :
             "usage: wifi-change\r\n");
}

static void command_tirtc_clear(char *output, int capacity,
                                int argc, char **argv)
{
    (void)argv;
    int rc = argc == 1 ? xiaotai_storage_clear_device() : BK_ERR_PARAM;
    if (rc == BK_OK) rc = schedule_reboot();
    response(output, capacity, rc == BK_OK ?
             "device credentials cleared; restarting into binding\r\n" :
             argc == 1 ? "device credentials clear failed\r\n" :
             "usage: tirtc-clear\r\n");
}

static void command_wechat_call(char *output, int capacity,
                                int argc, char **argv)
{
    if (argc > 2) {
        response(output, capacity, "usage: wechat-call [\"contact name\"]\r\n");
        return;
    }
    int rc = xiaotai_app_request_call(argc == 2 ? argv[1] : NULL, true);
    response(output, capacity, rc == BK_OK ? "WeChat call requested\r\n" :
             "WeChat call request rejected\r\n");
}

static void command_device_call(char *output, int capacity,
                                int argc, char **argv)
{
    if (argc != 2) {
        response(output, capacity, "usage: device-call \"contact name\"\r\n");
        return;
    }
    int rc = xiaotai_app_request_call(argv[1], false);
    response(output, capacity, rc == BK_OK ? "device call requested\r\n" :
             "device call request rejected\r\n");
}

static void command_call_answer_owner(char *output, int capacity,
                                      int argc,
                                      xiaotai_session_owner_t owner)
{
    int rc = argc == 1 ? xiaotai_app_request_call_answer(owner) : BK_ERR_PARAM;
    response(output, capacity, rc == BK_OK ? "call answer requested\r\n" :
             argc == 1 ? "call answer rejected\r\n" :
             "usage: call-answer\r\n");
}

static void command_call_answer(char *output, int capacity,
                                int argc, char **argv)
{
    (void)argv;
    command_call_answer_owner(output, capacity, argc, XIAOTAI_OWNER_NONE);
}

static void command_wechat_answer(char *output, int capacity,
                                  int argc, char **argv)
{
    (void)argv;
    command_call_answer_owner(output, capacity, argc,
                              XIAOTAI_OWNER_WECHAT_VOIP);
}

static void command_device_answer(char *output, int capacity,
                                  int argc, char **argv)
{
    (void)argv;
    command_call_answer_owner(output, capacity, argc,
                              XIAOTAI_OWNER_DEVICE_CALL);
}

static void command_call_hangup(char *output, int capacity,
                                int argc, char **argv)
{
    (void)argv;
    int rc = argc == 1 ? xiaotai_app_request_call_hangup() : BK_ERR_PARAM;
    response(output, capacity, rc == BK_OK ? "call hangup requested\r\n" :
             argc == 1 ? "call hangup rejected\r\n" :
             "usage: call-hangup\r\n");
}

static void command_call_status(char *output, int capacity,
                                int argc, char **argv)
{
    (void)argv;
    if (argc != 1) {
        response(output, capacity, "usage: call-status\r\n");
        return;
    }
    xiaotai_runtime_t runtime = {0};
    bool outbound = false;
    int rc = xiaotai_app_call_snapshot(&runtime, &outbound);
    if (rc != BK_OK) {
        response(output, capacity, "call runtime not ready\r\n");
        return;
    }
    if (output == NULL || capacity <= 0) return;
    snprintf(output, (size_t)capacity,
             "channel=%s direction=%s state=%d generation=%u deadline_ms=%u\r\n",
             owner_name(runtime.owner), outbound ? "outbound" : "inbound",
             (int)runtime.state, (unsigned)runtime.generation,
             (unsigned)runtime.deadline_ms);
}

static void command_room_join(char *output, int capacity,
                              int argc, char **argv)
{
    if (argc < 2 || argc > 3) {
        response(output, capacity,
                 "usage: room-join <6-digit-room-code> [4-digit-password]\r\n");
        return;
    }
    int rc = xiaotai_app_request_room_join(argv[1],
                                            argc == 3 ? argv[2] : NULL);
    response(output, capacity, rc == BK_OK ? "room join requested\r\n" :
             rc == BK_ERR_PARAM ? "invalid room code or password\r\n" :
             "room join request rejected\r\n");
}

static void command_room_leave(char *output, int capacity,
                               int argc, char **argv)
{
    (void)argv;
    int rc = argc == 1 ? xiaotai_app_request_room_leave() : BK_ERR_PARAM;
    response(output, capacity, rc == BK_OK ? "room leave requested\r\n" :
             argc == 1 ? "room leave request rejected\r\n" :
             "usage: room-leave\r\n");
}

static void command_room_talk(char *output, int capacity, int argc,
                              bool enabled)
{
    int rc = argc == 1 ? xiaotai_app_request_room_talk(enabled) : BK_ERR_PARAM;
    response(output, capacity, rc == BK_OK ?
             (enabled ? "room talk start requested\r\n" :
                        "room talk stop requested\r\n") :
             argc == 1 ? "room talk request rejected\r\n" :
             enabled ? "usage: room-talk-start\r\n" :
                       "usage: room-talk-stop\r\n");
}

static void command_room_talk_start(char *output, int capacity,
                                    int argc, char **argv)
{
    (void)argv;
    command_room_talk(output, capacity, argc, true);
}

static void command_room_talk_stop(char *output, int capacity,
                                   int argc, char **argv)
{
    (void)argv;
    command_room_talk(output, capacity, argc, false);
}

static void command_room_sync(char *output, int capacity,
                              int argc, char **argv)
{
    (void)argv;
    int rc = argc == 1 ? xiaotai_app_request_room_sync() : BK_ERR_PARAM;
    response(output, capacity, rc == BK_OK ? "room sync requested\r\n" :
             argc == 1 ? "room sync request rejected\r\n" :
             "usage: room-sync\r\n");
}

static void command_room_status(char *output, int capacity,
                                int argc, char **argv)
{
    (void)argv;
    if (argc != 1) {
        response(output, capacity, "usage: room-status\r\n");
        return;
    }
    xiaotai_room_snapshot_t room = {0};
    if (xiaotai_app_room_snapshot(&room) != BK_OK) {
        response(output, capacity, "room runtime not ready\r\n");
        return;
    }
    if (output == NULL || capacity <= 0) return;
    snprintf(output, (size_t)capacity,
             "assigned=%d pending=%d connecting=%d joined=%d talking=%d "
             "code=%s members=%u assignment_version=%lld\r\n",
             room.assigned ? 1 : 0, room.request_pending ? 1 : 0,
             room.connecting ? 1 : 0, room.joined ? 1 : 0,
             room.talking ? 1 : 0,
             room.room_code[0] == '\0' ? "-" : room.room_code,
             room.members, (long long)room.assignment_version);
}

static void command_restart(char *output, int capacity, int argc, char **argv)
{
    (void)argv;
    int rc = argc == 1 ? schedule_reboot() : BK_ERR_PARAM;
    response(output, capacity, rc == BK_OK ? "restarting\r\n" :
             argc == 1 ? "restart scheduling failed\r\n" :
             "usage: restart\r\n");
}

static bool parse_log_level(const char *value, int maximum, int *level)
{
    char *end = NULL;
    long parsed = strtol(value, &end, 10);
    if (value[0] == '\0' || end == NULL || *end != '\0' ||
        parsed < 0 || parsed > maximum) {
        return false;
    }
    *level = (int)parsed;
    return true;
}

static void command_log_level(char *output, int capacity,
                              int argc, char **argv)
{
    if (argc == 1) {
        if (output != NULL && capacity > 0) {
            int tirtc_level = xiaotai_tirtc_get_log_level();
            if (tirtc_level == XIAOTAI_TIRTC_LOG_LEVEL_SDK_DEFAULT) {
                snprintf(output, (size_t)capacity,
                         "tirtc=default app=%d (tirtc 0..17, app 0..%d)\r\n",
                         xiaotai_log_get_level(), XIAOTAI_APP_LOG_MAX);
            } else {
                snprintf(output, (size_t)capacity,
                         "tirtc=%d app=%d (tirtc 0..17, app 0..%d)\r\n",
                         tirtc_level, xiaotai_log_get_level(),
                         XIAOTAI_APP_LOG_MAX);
            }
        }
        return;
    }
    if (argc != 3) {
        response(output, capacity,
                 "usage: log-level [tirtc <0..17>|app <0..4>]\r\n");
        return;
    }
    int level = 0;
    int rc = BK_ERR_PARAM;
    if (strcmp(argv[1], "tirtc") == 0 &&
        parse_log_level(argv[2], 17, &level)) {
        rc = xiaotai_tirtc_set_log_level(level);
    } else if (strcmp(argv[1], "app") == 0 &&
               parse_log_level(argv[2], XIAOTAI_APP_LOG_MAX, &level)) {
        rc = xiaotai_log_set_level(level);
    }
    if (rc == BK_OK && output != NULL && capacity > 0) {
        snprintf(output, (size_t)capacity, "%s log level=%d\r\n",
                 argv[1], level);
    } else {
        response(output, capacity,
                 "usage: log-level [tirtc <0..17>|app <0..4>]\r\n");
    }
}

static const struct cli_command s_commands[] = {
    {"status", "Show XiaoTai runtime and resource status", command_status},
    {"ai-start", "Start AI talk", command_ai_start},
    {"ai-stop", "Stop AI talk", command_ai_stop},
    {"wifi-set", "wifi-set <ssid> <password>", command_wifi_set},
    {"wifi-clear", "Clear Wi-Fi and restart into SoftAP", command_wifi_clear},
    {"wificlear", "Compatibility alias for wifi-clear", command_wifi_clear},
    {"wifi-change", "Open provisioning and retain credentials", command_wifi_change},
    {"tirtc-clear", "Clear binding and restart", command_tirtc_clear},
    {"wechat-call", "wechat-call [contact name]", command_wechat_call},
    {"device-call", "device-call <contact name>", command_device_call},
    {"call-answer", "Answer the current incoming call", command_call_answer},
    {"wechat-answer", "Answer only a WeChat incoming call", command_wechat_answer},
    {"device-answer", "Answer only a device incoming call", command_device_answer},
    {"call-hangup", "Hang up the current call", command_call_hangup},
    {"call-status", "Show current call state", command_call_status},
    {"room-join", "room-join <room code> [password]", command_room_join},
    {"room-leave", "Leave the assigned group room", command_room_leave},
    {"room-talk-start", "Start group-room PTT uplink", command_room_talk_start},
    {"room-talk-stop", "Stop group-room PTT uplink", command_room_talk_stop},
    {"room-sync", "Refresh group-room assignment", command_room_sync},
    {"room-status", "Show group-room state", command_room_status},
    {"log-level", "log-level [tirtc <0..17>|app <0..4>]", command_log_level},
    {"restart", "Restart the device", command_restart},
};

int xiaotai_console_start(void)
{
    return cli_register_commands(s_commands,
        (int)(sizeof(s_commands) / sizeof(s_commands[0])));
}
