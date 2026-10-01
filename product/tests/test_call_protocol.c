#include <assert.h>
#include <stdlib.h>
#include <string.h>

#include "xiaotai_call_protocol.h"

int main(void)
{
    char peer[65] = {0};
    char token[65] = {0};
    assert(xiaotai_call_decode_info(
        "{\"code\":200,\"data\":{\"device_id\":\"peer-1\","
        "\"token\":\"secret\"}}", peer, sizeof(peer), token,
        sizeof(token)));
    assert(strcmp(peer, "peer-1") == 0 && strcmp(token, "secret") == 0);
    assert(!xiaotai_call_decode_info("{\"code\":500}", peer,
                                     sizeof(peer), token, sizeof(token)));

    char identifier[128] = {0};
    assert(xiaotai_call_decode_outbound(
        "{\"code\":0,\"data\":{\"room_id\":\"room-1\"}}", false,
        identifier, sizeof(identifier)));
    assert(strcmp(identifier, "room-1") == 0);
    assert(xiaotai_call_decode_outbound_result(
        "{\"code\":40201,\"msg\":\"所有目标设备均不在线\"}", false,
        identifier, sizeof(identifier)) == XIAOTAI_CALL_OUTBOUND_OFFLINE);
    assert(xiaotai_call_decode_outbound_result(
        "{\"code\":500,\"msg\":\"internal error\"}", false,
        identifier, sizeof(identifier)) == XIAOTAI_CALL_OUTBOUND_FAILED);
    assert(xiaotai_call_decode_outbound_result(
        "not-json", false, identifier, sizeof(identifier)) ==
        XIAOTAI_CALL_OUTBOUND_INVALID);

    char json[256];
    assert(xiaotai_call_encode_device_info(json, sizeof(json),
                                            "peer-1", "room-1") > 0);
    assert(strstr(json, "\"purpose\":\"call\"") != NULL);
    assert(xiaotai_call_encode_device_dial(json, sizeof(json), "peer-1") > 0);
    assert(xiaotai_call_encode_voip_dial(json, sizeof(json), "device-1",
                                         "openid-1", "model-1",
                                         "app-1") > 0);
    assert(strstr(json, "\"wx_app_id\":\"app-1\"") != NULL);
    assert(strstr(json, "\"calling_timeout_sec\":30") != NULL);
    assert(xiaotai_call_encode_voip_dial(json, sizeof(json), "device-1",
                                         "openid-1", "model-1", NULL) > 0);
    assert(strstr(json, "wx_app_id") == NULL);
    assert(xiaotai_call_encode_device_end(json, sizeof(json), "room-1",
                                          "timeout", false) > 0);
    assert(strstr(json, "timeout") != NULL);

    char *reject = xiaotai_call_encode_voip_reject(
        "app", "model", "token", "room", "payload", 7);
    assert(reject != NULL && strstr(reject, "\"hangup_reason\":7") != NULL);
    free(reject);
    assert(xiaotai_call_encode_voip_reject(
        "", "model", "token", "room", "payload", 7) == NULL);
    return 0;
}
