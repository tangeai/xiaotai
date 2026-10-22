#ifndef XIAOTAI_APP_H
#define XIAOTAI_APP_H

#include "xiaotai_runtime.h"
#include "xiaotai_room.h"

int xiaotai_app_start(void);
int xiaotai_app_request_ai_start(void);
int xiaotai_app_request_ai_stop(void);
int xiaotai_app_runtime_snapshot(xiaotai_runtime_t *out);
int xiaotai_app_request_call(const char *contact_name, bool wechat);
int xiaotai_app_request_call_answer(xiaotai_session_owner_t expected_owner);
int xiaotai_app_request_call_hangup(void);
int xiaotai_app_call_snapshot(xiaotai_runtime_t *out, bool *outbound);
int xiaotai_app_request_room_join(const char *room_code,
                                  const char *password);
int xiaotai_app_request_room_leave(void);
int xiaotai_app_request_room_talk(bool enabled);
int xiaotai_app_request_room_sync(void);
int xiaotai_app_room_snapshot(xiaotai_room_snapshot_t *out);

#endif
