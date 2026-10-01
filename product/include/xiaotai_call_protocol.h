#ifndef XIAOTAI_CALL_PROTOCOL_H
#define XIAOTAI_CALL_PROTOCOL_H

#include <stdbool.h>
#include <stddef.h>

typedef enum {
    XIAOTAI_CALL_OUTBOUND_ACCEPTED = 0,
    XIAOTAI_CALL_OUTBOUND_OFFLINE,
    XIAOTAI_CALL_OUTBOUND_FAILED,
    XIAOTAI_CALL_OUTBOUND_INVALID,
} xiaotai_call_outbound_result_t;

bool xiaotai_call_decode_info(const char *json, char *peer, size_t peer_size,
                              char *token, size_t token_size);
xiaotai_call_outbound_result_t xiaotai_call_decode_outbound_result(
    const char *json, bool wechat, char *identifier, size_t identifier_size);
bool xiaotai_call_decode_outbound(const char *json, bool wechat,
                                  char *identifier, size_t identifier_size);
int xiaotai_call_encode_device_info(char *output, size_t capacity,
                                    const char *peer, const char *room_id);
int xiaotai_call_encode_device_dial(char *output, size_t capacity,
                                    const char *peer);
int xiaotai_call_encode_voip_dial(char *output, size_t capacity,
                                  const char *device_id,
                                  const char *openid,
                                  const char *model_id,
                                  const char *app_id);
int xiaotai_call_encode_device_end(char *output, size_t capacity,
                                   const char *room_id, const char *reason,
                                   bool cancel);
char *xiaotai_call_encode_voip_reject(const char *app_id,
                                      const char *model_id,
                                      const char *server_token,
                                      const char *room_id,
                                      const char *payload, int reason);

#endif
