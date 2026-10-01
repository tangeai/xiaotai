#ifndef XIAOTAI_ROOM_H
#define XIAOTAI_ROOM_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define XIAOTAI_ROOM_COMMAND 0x2200U
#define XIAOTAI_ROOM_PARTICIPANTS_MAX 8U

typedef struct {
    char id[65];
    char name[65];
    bool self;
    bool speaking;
} xiaotai_room_participant_t;

typedef struct {
    bool assigned;
    bool request_pending;
    bool connecting;
    bool joined;
    bool talking;
    unsigned members;
    size_t participant_count;
    xiaotai_room_participant_t participants[XIAOTAI_ROOM_PARTICIPANTS_MAX];
    int64_t assignment_version;
    char room_code[7];
} xiaotai_room_snapshot_t;

typedef enum {
    XIAOTAI_ROOM_RESPONSE_ASSIGNMENT = 1,
    XIAOTAI_ROOM_RESPONSE_TOKEN,
    XIAOTAI_ROOM_RESPONSE_PRESENCE,
} xiaotai_room_response_t;

typedef enum {
    XIAOTAI_ROOM_ACTION_CREATE = 1,
    XIAOTAI_ROOM_ACTION_JOIN,
    XIAOTAI_ROOM_ACTION_LEAVE,
    XIAOTAI_ROOM_ACTION_TALK_START,
    XIAOTAI_ROOM_ACTION_TALK_STOP,
    XIAOTAI_ROOM_ACTION_SYNC,
    XIAOTAI_ROOM_ACTION_END,
} xiaotai_room_action_t;

typedef enum {
    XIAOTAI_ROOM_DIAG_ASSIGNMENT_REQUEST_FAILED = 1,
    XIAOTAI_ROOM_DIAG_ASSIGNMENT_INVALID,
    XIAOTAI_ROOM_DIAG_TOKEN_STALE,
    XIAOTAI_ROOM_DIAG_TOKEN_CONNECT_FAILED,
    XIAOTAI_ROOM_DIAG_PRESENCE_REJECTED,
    XIAOTAI_ROOM_DIAG_TRANSPORT_STALE,
    XIAOTAI_ROOM_DIAG_JOIN_SEND_FAILED,
    XIAOTAI_ROOM_DIAG_JOIN_REJECTED,
    XIAOTAI_ROOM_DIAG_MEDIA_START_FAILED,
    XIAOTAI_ROOM_DIAG_LEASE_EXPIRED,
    XIAOTAI_ROOM_DIAG_DISCONNECTED,
    XIAOTAI_ROOM_DIAG_LEAVE_FAILED,
} xiaotai_room_diagnostic_t;

typedef void (*xiaotai_room_service_response_fn)(const char *body,
                                                  void *context);

typedef struct {
    int (*service_request)(const char *path, const char *body,
                           xiaotai_room_service_response_fn callback,
                           void *callback_context, void *context);
    uint32_t (*now_ms)(void *context);
    bool (*random_bytes)(void *buffer, size_t size, void *context);
    bool (*service_ready)(void *context);
    bool (*transport_busy)(void *context);
    bool (*transport_connected)(void *context);
    int (*transport_connect)(const char *peer, const char *token,
                             uint32_t generation, void *context);
    int (*transport_start_media)(void *context);
    int (*transport_send)(uint32_t command, const void *data,
                          uint32_t length, void *context);
    int (*transport_disconnect)(void *context);
    void (*set_uplink_enabled)(bool enabled, void *context);
    void (*post_response)(xiaotai_room_response_t type, uint32_t generation,
                          const char *text, size_t length, void *context);
    bool (*media_acquire)(uint32_t *runtime_generation, void *context);
    bool (*media_activate)(uint32_t runtime_generation, void *context);
    void (*media_release)(uint32_t runtime_generation, void *context);
    bool (*media_available)(void *context);
    void (*state_changed)(void *context);
    void (*transport_closed)(void *context);
    void (*diagnostic)(xiaotai_room_diagnostic_t event, uint32_t generation,
                       int detail, void *context);
    void *context;
} xiaotai_room_port_t;

/* The context is caller-owned and contains no dynamically allocated memory. */
typedef struct {
    xiaotai_room_port_t port;
    bool assigned;
    bool request_pending;
    bool foreground;
    bool leave_pending;
    bool connecting;
    bool joined;
    bool ptt;
    uint32_t generation;
    uint32_t runtime_generation;
    bool assignment_refresh_requested;
    uint32_t next_heartbeat_ms;
    uint32_t lease_deadline_ms;
    uint32_t heartbeat_seconds;
    uint32_t lease_seconds;
    int64_t assignment_version;
    unsigned members;
    size_t participant_count;
    xiaotai_room_participant_t participants[XIAOTAI_ROOM_PARTICIPANTS_MAX];
    char device_id[65];
    char room_id[65];
    char room_code[7];
    char session_id[65];
} xiaotai_room_t;

void xiaotai_room_init(xiaotai_room_t *room, const char *device_id,
                       const xiaotai_room_port_t *port);
void xiaotai_room_set_device_id(xiaotai_room_t *room, const char *device_id);
void xiaotai_room_tick(xiaotai_room_t *room);
void xiaotai_room_handle_response(xiaotai_room_t *room,
                                  xiaotai_room_response_t type,
                                  uint32_t generation, const char *text);
void xiaotai_room_handle_connected(xiaotai_room_t *room,
                                   uint32_t generation);
void xiaotai_room_handle_command(xiaotai_room_t *room, uint32_t generation,
                                 const char *text);
void xiaotai_room_handle_disconnected(xiaotai_room_t *room,
                                      uint32_t generation, int error);
int xiaotai_room_action(xiaotai_room_t *room, xiaotai_room_action_t action,
                        const char *json);
void xiaotai_room_disconnect(xiaotai_room_t *room, const char *presence);
void xiaotai_room_assignment_changed(xiaotai_room_t *room);
void xiaotai_room_closed(xiaotai_room_t *room);
bool xiaotai_room_active(const xiaotai_room_t *room);
bool xiaotai_room_joined(const xiaotai_room_t *room);
void xiaotai_room_snapshot(const xiaotai_room_t *room,
                           xiaotai_room_snapshot_t *out);

#endif
