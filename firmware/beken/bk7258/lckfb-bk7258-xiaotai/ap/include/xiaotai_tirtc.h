#ifndef XIAOTAI_TIRTC_H
#define XIAOTAI_TIRTC_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define XIAOTAI_TIRTC_LOG_LEVEL_SDK_DEFAULT (-1)

#include "tirtc/tiRTC.h"

typedef enum {
    XIAOTAI_TIRTC_STOPPED = 0,
    XIAOTAI_TIRTC_STARTING,
    XIAOTAI_TIRTC_READY,
    XIAOTAI_TIRTC_STOPPING,
    XIAOTAI_TIRTC_FAILED,
} xiaotai_tirtc_state_t;

typedef struct {
    const char *device_id;
    const char *device_secret;
    const char *client_id;
    const char *app_id;
    const char *service_endpoint;
    size_t max_send_buffer;
    int max_connections;
    int poll_timeout_ms;
} xiaotai_tirtc_config_t;

typedef struct {
    /** Runs on the TiRTC product worker, outside the SDK callback stack. */
    bool (*on_stream_connected)(uint32_t generation, void *context);
    void (*on_stream_disconnected)(uint32_t generation,
                                   int error,
                                   void *context);
    void (*on_ai_connected)(uint32_t generation, void *context);
    void (*on_ai_command)(uint32_t generation, uint32_t command,
                          const void *data, uint32_t length, void *context);
    void (*on_ai_disconnected)(uint32_t generation,
                               int error,
                               void *context);
    void (*on_call_connected)(uint32_t generation, void *context);
    void (*on_call_disconnected)(uint32_t generation,
                                 int error,
                                 void *context);
    void (*on_voip_connected)(uint32_t generation, void *context);
    void (*on_voip_active)(uint32_t generation, void *context);
    void (*on_voip_disconnected)(uint32_t generation,
                                 int error,
                                 void *context);
    void (*on_room_connected)(uint32_t generation, void *context);
    void (*on_room_command)(uint32_t generation, uint32_t command,
                            const void *data, uint32_t length, void *context);
    void (*on_room_disconnected)(uint32_t generation,
                                 int error,
                                 void *context);
    void *context;
} xiaotai_tirtc_handlers_t;

void xiaotai_tirtc_set_handlers(const xiaotai_tirtc_handlers_t *handlers);
int xiaotai_tirtc_start(const xiaotai_tirtc_config_t *config);
int xiaotai_tirtc_stop(void);
/** Call from the state-owning task after the SYS_STOPPED event. */
int xiaotai_tirtc_finalize_stop(void);
xiaotai_tirtc_state_t xiaotai_tirtc_state(void);
bool xiaotai_tirtc_ready(void);
bool xiaotai_tirtc_connected(void);
/** True while connected, connecting, or draining a failed connection. */
bool xiaotai_tirtc_busy(void);
/**
 * Poll null-handle cleanup and report a poisoned TiRTC runtime. The
 * state-owning Coordinator calls this periodically; true requires a bounded
 * lifecycle recycle before another outgoing session.
 */
bool xiaotai_tirtc_recovery_required(void);
/** Compare post-restart internal heap with the failed connection baseline. */
bool xiaotai_tirtc_recovery_resources_restored(void);
/** Emit one compact STREAM media-stage diagnostic line. */
void xiaotai_tirtc_log_stream_media_state(void);
uint32_t xiaotai_tirtc_generation(void);
const char *xiaotai_tirtc_version(void);
/** Set the TiRTC SDK log level at runtime (0..17). Not persisted. */
int xiaotai_tirtc_set_log_level(int level);
/** Return -1 until an explicit runtime override is applied. */
int xiaotai_tirtc_get_log_level(void);

/** Device-authenticated TiRTC service request. Callback runs on an SDK task. */
int xiaotai_tirtc_service_request(const char *path,
                                  TIRTCSERVICEREQUESTCALLBACK callback,
                                  void *context);
int xiaotai_tirtc_service_request_json(const char *path, const char *json,
                                       TIRTCSERVICEREQUESTCALLBACK callback,
                                       void *context);
int xiaotai_tirtc_ai_connect(const char *peer_id, const char *token,
                             uint32_t generation);
/** Prepare Opus capture/playback before start_session can produce audio. */
int xiaotai_tirtc_ai_prepare_media(void);
int xiaotai_tirtc_ai_start_media(void);
int xiaotai_tirtc_call_connect(const char *peer_id, const char *token,
                               const char *room_id, uint32_t generation);
/* Arms the caller side before the callee opens its inbound TiRTC connection. */
int xiaotai_tirtc_expect_outbound_call(const char *room_id,
                                       uint32_t generation);
int xiaotai_tirtc_voip_connect(const char *peer_id, const char *token,
                               uint32_t generation);
int xiaotai_tirtc_room_connect(const char *peer_id, const char *token,
                               uint32_t generation);
/** Start room audio after the join_room response validates the profile. */
int xiaotai_tirtc_room_start_media(void);
int xiaotai_tirtc_send_command(uint32_t command,
                               const void *data, uint32_t length);
int xiaotai_tirtc_disconnect(void);

#endif
