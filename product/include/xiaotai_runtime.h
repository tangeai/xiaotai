#ifndef XIAOTAI_RUNTIME_H
#define XIAOTAI_RUNTIME_H

#include <stdbool.h>
#include <stdint.h>

typedef enum {
    XIAOTAI_OWNER_NONE = 0,
    XIAOTAI_OWNER_STREAM,
    XIAOTAI_OWNER_AI,
    XIAOTAI_OWNER_DEVICE_CALL,
    XIAOTAI_OWNER_WECHAT_VOIP,
    XIAOTAI_OWNER_ROOM,
} xiaotai_session_owner_t;

typedef enum {
    XIAOTAI_STATE_WAITING = 0,
    XIAOTAI_STATE_STREAM_ACTIVE,
    XIAOTAI_STATE_AI_CONNECTING,
    XIAOTAI_STATE_AI_ACTIVE,
    XIAOTAI_STATE_CALL_INCOMING,
    XIAOTAI_STATE_CALL_CONNECTING,
    XIAOTAI_STATE_CALL_ACTIVE,
    XIAOTAI_STATE_CALL_ENDING,
    XIAOTAI_STATE_ROOM_CONNECTING,
    XIAOTAI_STATE_ROOM_ACTIVE,
} xiaotai_runtime_state_t;

typedef struct {
    xiaotai_session_owner_t owner;
    xiaotai_runtime_state_t state;
    uint32_t generation;
    uint32_t deadline_ms;
    xiaotai_session_owner_t pending_incoming;
    uint32_t pending_generation;
    uint32_t pending_deadline_ms;
} xiaotai_runtime_t;

void xiaotai_runtime_init(xiaotai_runtime_t *runtime);
bool xiaotai_runtime_begin(xiaotai_runtime_t *runtime,
                           xiaotai_session_owner_t owner,
                           bool incoming);
bool xiaotai_runtime_connected(xiaotai_runtime_t *runtime,
                               uint32_t generation);
bool xiaotai_runtime_media_started(xiaotai_runtime_t *runtime,
                                   uint32_t generation);
/* Move an active/connecting call into bounded local teardown. */
bool xiaotai_runtime_begin_ending(xiaotai_runtime_t *runtime,
                                  uint32_t generation,
                                  uint32_t now_ms,
                                  uint32_t timeout_ms);
bool xiaotai_runtime_arm_timeout(xiaotai_runtime_t *runtime,
                                 uint32_t generation,
                                 uint32_t now_ms,
                                 uint32_t timeout_ms);
bool xiaotai_runtime_expire(xiaotai_runtime_t *runtime,
                            uint32_t now_ms,
                            xiaotai_session_owner_t *owner,
                            xiaotai_runtime_state_t *state,
                            uint32_t *generation);
bool xiaotai_runtime_finish(xiaotai_runtime_t *runtime,
                            uint32_t generation);

/* Incoming signaling does not own media. One device/WeChat call may wait
 * while STREAM, AI or Room is active; a second incoming call is busy. */
bool xiaotai_runtime_offer_incoming(xiaotai_runtime_t *runtime,
                                    xiaotai_session_owner_t owner,
                                    uint32_t now_ms,
                                    uint32_t timeout_ms);
bool xiaotai_runtime_has_incoming(const xiaotai_runtime_t *runtime);
/* The home/idle presentation is allowed only when neither media nor an
 * incoming-call prompt owns the foreground UI. */
bool xiaotai_runtime_home_allowed(const xiaotai_runtime_t *runtime);
bool xiaotai_runtime_accept_incoming(xiaotai_runtime_t *runtime,
                                     xiaotai_session_owner_t *owner,
                                     uint32_t *generation);
bool xiaotai_runtime_cancel_incoming(xiaotai_runtime_t *runtime,
                                     xiaotai_session_owner_t *owner);
bool xiaotai_runtime_expire_incoming(xiaotai_runtime_t *runtime,
                                     uint32_t now_ms,
                                     xiaotai_session_owner_t *owner);

/* Current products expose one TiRTC/media slot. */
bool xiaotai_runtime_remote_view_allowed(const xiaotai_runtime_t *runtime,
                                         bool room_active);

#endif
