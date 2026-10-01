#ifndef XIAOTAI_SIGNAL_H
#define XIAOTAI_SIGNAL_H

#include <stdbool.h>

typedef enum {
    XIAOTAI_SIGNAL_UNBIND = 1,
    XIAOTAI_SIGNAL_DEVICE_CALL_INCOMING,
    XIAOTAI_SIGNAL_VOIP_CALL_INCOMING,
    XIAOTAI_SIGNAL_CONTACTS_CHANGED,
    XIAOTAI_SIGNAL_ROOM_ASSIGNMENT_CHANGED,
    XIAOTAI_SIGNAL_ROOM_CLOSED,
    XIAOTAI_SIGNAL_CALL_ENDED,
    XIAOTAI_SIGNAL_UNKNOWN,
} xiaotai_signal_type_t;

/* String pointers are borrowed from the decoder and remain valid only during
 * the visitor call. The visitor must copy anything retained asynchronously. */
typedef struct {
    xiaotai_signal_type_t type;
    const char *name;
    const char *room_id;
    const char *caller_id;
    const char *call_type;
    const char *peer_id;
    const char *token;
    const char *wx_room_id;
    const char *wx_call_id;
    const char *wx_user_openid;
    const char *wx_from;
    const char *wx_app_id;
    const char *wx_model_id;
    const char *wx_server_token;
    const char *wx_payload;
} xiaotai_signal_view_t;

typedef void (*xiaotai_signal_visitor_fn)(const xiaotai_signal_view_t *signal,
                                          void *context);

/* Returns false for malformed envelopes. Unknown, well-formed event types are
 * delivered as XIAOTAI_SIGNAL_UNKNOWN so adapters may log them. */
bool xiaotai_signal_decode(const char *json, xiaotai_signal_visitor_fn visitor,
                           void *context);

#endif
