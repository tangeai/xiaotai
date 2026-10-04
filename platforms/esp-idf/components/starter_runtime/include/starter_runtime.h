#ifndef STARTER_RUNTIME_H
#define STARTER_RUNTIME_H

/**
 * @file starter_runtime.h
 * @brief H5 与 AI 共用媒体资源的会话状态机。
 *
 * 本模块隐藏 token 获取、WHIP 建连、AI JSON-RPC 握手、超时和迟到回调过滤。
 * 所有状态变化都在一个 FreeRTOS 任务内串行执行；调用者只投递意图并读取快照。
 */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"
#include "xiaotai_runtime.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Public ESP snapshots use the product runtime's canonical state values. */
typedef xiaotai_runtime_state_t starter_runtime_state_t;
#define STARTER_RUNTIME_WAITING XIAOTAI_STATE_WAITING
#define STARTER_RUNTIME_H5_ACTIVE XIAOTAI_STATE_STREAM_ACTIVE
#define STARTER_RUNTIME_AI_CONNECTING XIAOTAI_STATE_AI_CONNECTING
#define STARTER_RUNTIME_AI_ACTIVE XIAOTAI_STATE_AI_ACTIVE
#define STARTER_RUNTIME_CALL_INCOMING XIAOTAI_STATE_CALL_INCOMING
#define STARTER_RUNTIME_CALL_CONNECTING XIAOTAI_STATE_CALL_CONNECTING
#define STARTER_RUNTIME_CALL_ACTIVE XIAOTAI_STATE_CALL_ACTIVE
#define STARTER_RUNTIME_ROOM_CONNECTING XIAOTAI_STATE_ROOM_CONNECTING
#define STARTER_RUNTIME_ROOM_ACTIVE XIAOTAI_STATE_ROOM_ACTIVE

typedef enum {
    STARTER_CONTACT_DEVICE = 0,
    STARTER_CONTACT_WECHAT,
} starter_contact_source_t;

#define STARTER_PRODUCT_CONTACTS_MAX 8

typedef struct {
    starter_contact_source_t source;
    bool online; /**< 只对 device 有意义；微信联系人不得伪造在线状态。 */
    char id[65];
    char name[65];
    char wx_app_id[65];
    char wx_model_id[65];
} starter_product_contact_t;

typedef struct {
    starter_runtime_state_t state; /**< 当前公开状态。 */
    uint32_t session_generation;   /**< 每次业务会话递增，用于过滤 HTTP 响应。 */
    uint32_t connection_generation; /**< TiRTC 连接代次，0 表示无连接。 */
    int last_error;                /**< 最近一次结束原因，0 表示正常结束。 */
    uint32_t stack_high_water_bytes; /**< 状态任务历史最小剩余栈，单位字节。 */
} starter_runtime_status_t;

#define STARTER_DIAGNOSTIC_EVENTS 8
typedef struct {
    uint32_t at_ms;
    uint32_t session;
    int value;
    char label[32]; /**< Fixed event label; never user text, identifiers or tokens. */
} starter_diagnostic_event_t;
typedef struct {
    unsigned count;
    starter_diagnostic_event_t events[STARTER_DIAGNOSTIC_EVENTS]; /**< Oldest first. */
} starter_runtime_diagnostics_t;
void starter_runtime_diagnostics(starter_runtime_diagnostics_t *out);

typedef enum {
    STARTER_AI_UI_IDLE = 0,
    STARTER_AI_UI_LISTENING,
    STARTER_AI_UI_THINKING,
    STARTER_AI_UI_SPEAKING,
} starter_ai_ui_phase_t;

typedef enum {
    STARTER_ROOM_NONE = 0,
    STARTER_ROOM_ASSIGNED,
    STARTER_ROOM_CONNECTING,
    STARTER_ROOM_JOINED,
    STARTER_ROOM_ERROR,
} starter_room_ui_phase_t;

#define STARTER_ROOM_MEMBERS_MAX 8
typedef struct {
    char id[65];
    bool self;
    bool speaking;
} starter_room_member_t;

/** 面向屏幕的有界快照；字幕最多两行显示，完整历史由上层自行持久化。 */
typedef struct {
    starter_ai_ui_phase_t ai_phase;
    bool ai_start_pending; /**< 用户已请求 AI，正在等待平台和 TiRTC 就绪。 */
    bool caption_is_ai;
    bool caption_final;
    char subtitle[193];
    char emotion[16];
    bool call_incoming;
    bool call_wechat;
    bool call_microphone_muted;
    bool call_video;
    bool call_camera_enabled;
    uint8_t contact_count;
    char call_peer[65];
    char call_result[33];
    bool wechat_contacts_checked;
    uint8_t wechat_contact_count;
    starter_room_ui_phase_t room_phase;
    bool room_password_set;
    bool room_ptt;
    uint8_t room_online_count;
    uint8_t room_member_count;
    char room_code[7];
    char room_message[49];
    starter_room_member_t room_members[STARTER_ROOM_MEMBERS_MAX];
    starter_product_contact_t contacts[STARTER_PRODUCT_CONTACTS_MAX];
} starter_runtime_product_snapshot_t;

/**
 * 创建状态任务并注册平台/TiRTC 回调。必须在 platform_client_start() 和
 * starter_tirtc_start() 前完成；重复调用安全。
 */
esp_err_t starter_runtime_start(const char *device_id);

/**
 * 在 TiRTC 已完成启动、MQTT 尚未创建前保留下一次外连所需的连续内部堆。
 * 连接门禁会在销毁 MQTT 后立即释放该块；调用方仅在启动编排阶段调用一次。
 */
esp_err_t starter_runtime_arm_external_connect_reserve(void);

/** 非阻塞请求启动 AI 对讲；返回值只表示事件是否成功入队。 */
esp_err_t starter_runtime_ai_start(void);
esp_err_t starter_runtime_ai_start_from_wake(uint32_t wake_token);

/** 非阻塞请求结束 AI 对讲；返回值只表示事件是否成功入队。 */
esp_err_t starter_runtime_ai_stop(void);

/** 联系人同步与纯语音呼叫控制，均只把意图投递给统一状态任务。 */
esp_err_t starter_runtime_contacts_refresh(void);
esp_err_t starter_runtime_wechat_quick_call(void);
esp_err_t starter_runtime_call_contact(uint8_t index);
#if CONFIG_IDF_TARGET_ESP32P4
esp_err_t starter_runtime_call_contact_video(uint8_t index);
esp_err_t starter_runtime_call_set_camera_enabled(uint32_t session_generation, bool enabled);
#endif
esp_err_t starter_runtime_call_accept(void);
esp_err_t starter_runtime_call_reject(void);
esp_err_t starter_runtime_call_hangup(void);
esp_err_t starter_runtime_call_set_microphone_muted(bool muted);

/** 多人对讲控制；房间号固定 6 位，密码为空或 4 位数字。 */
esp_err_t starter_runtime_room_refresh(void);
esp_err_t starter_runtime_room_create(const char *password);
esp_err_t starter_runtime_room_join(const char *room_code, const char *password);
esp_err_t starter_runtime_room_leave(void);
esp_err_t starter_runtime_room_ptt(bool pressed);

/** 返回线程安全的瞬时状态快照。 */
starter_runtime_status_t starter_runtime_status(void);

/** 复制当前 AI 字幕/阶段；内部使用互斥量，不返回运行时持有的指针。 */
starter_runtime_product_snapshot_t starter_runtime_product_snapshot(void);

/** Copies the immutable, provisioned device ID for product diagnostics. */
void starter_runtime_copy_device_id(char *out, size_t out_size);

/** 返回用于日志和串口状态输出的静态状态名称。 */
const char *starter_runtime_state_name(starter_runtime_state_t state);

#ifdef __cplusplus
}
#endif

#endif
