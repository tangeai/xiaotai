#include "xiaotai_ai_protocol.h"

#include <assert.h>
#include <string.h>

int main(void)
{
    char request[512];
    char action_result[512];
    int length = xiaotai_ai_encode_start(request, sizeof(request), "req-1",
                                         "dev-1", "role-1");
    assert(length > 0);
    assert(strstr(request, "\"method\":\"start_session\"") != NULL);
    assert(strstr(request, "\"codec\":\"opus\"") != NULL);
    assert(strstr(request, "\"sample_rate\":16000") != NULL);
    assert(xiaotai_ai_encode_start(request, 8, "req-1", "dev-1", "role-1") < 0);

    const char accepted[] =
        "{\"id\":\"req-1\",\"result\":{\"session_id\":\"s\","
        "\"input_audio\":{\"codec\":\"opus\",\"sample_rate\":16000,\"channels\":1},"
        "\"output_audio\":{\"codec\":\"opus\",\"sample_rate\":16000,\"channels\":1}}}";
    assert(xiaotai_ai_decode_message(accepted, strlen(accepted), "req-1") ==
           XIAOTAI_AI_MESSAGE_START_ACCEPTED);
    const char bad_profile[] =
        "{\"id\":\"req-1\",\"result\":{\"session_id\":\"s\","
        "\"input_audio\":{\"codec\":\"alaw\",\"sample_rate\":8000,\"channels\":1},"
        "\"output_audio\":{\"codec\":\"alaw\",\"sample_rate\":8000,\"channels\":1}}}";
    assert(xiaotai_ai_decode_message(bad_profile, strlen(bad_profile), "req-1") ==
           XIAOTAI_AI_MESSAGE_START_REJECTED);
    const char caption[] = "{\"method\":\"caption\",\"params\":{}}";
    const char emotion[] =
        "{\"method\":\"emotion\",\"params\":{\"emotion\":\"angry\"}}";
    const char ai_reply[] =
        "{\"type\":\"ai_reply\",\"text\":\"你好\",\"emotion\":\"happy\"}";
    const char emotion_event[] =
        "{\"method\":\"event\",\"params\":{\"type\":\"emotion\","
        "\"data\":{\"emotion\":\"crying\"}}}";
    const char call[] = "{\"method\":\"call_intent\",\"params\":{}}";
    const char device_action[] =
        "{\"jsonrpc\":\"2.0\",\"id\":7,\"method\":\"device_action\","
        "\"params\":{\"name\":\"call_contact\",\"arguments\":{"
        "\"contact_name\":\"爸爸\",\"call_type\":\"audio\"}}}";
    const char end[] = "{\"method\":\"end_session\"}";
    assert(xiaotai_ai_decode_message(caption, strlen(caption), "req-1") ==
           XIAOTAI_AI_MESSAGE_UI);
    assert(xiaotai_ai_decode_message(emotion, strlen(emotion), "req-1") ==
           XIAOTAI_AI_MESSAGE_UI);
    assert(xiaotai_ai_decode_message(ai_reply, strlen(ai_reply), "req-1") ==
           XIAOTAI_AI_MESSAGE_UI);
    assert(xiaotai_ai_decode_message(emotion_event, strlen(emotion_event),
                                     "req-1") == XIAOTAI_AI_MESSAGE_UI);
    assert(xiaotai_ai_decode_message(call, strlen(call), "req-1") ==
           XIAOTAI_AI_MESSAGE_CALL_INTENT);
    assert(xiaotai_ai_decode_message(device_action, strlen(device_action),
                                     "req-1") ==
           XIAOTAI_AI_MESSAGE_CALL_INTENT);
    int action_length = xiaotai_ai_encode_call_action_result(
        action_result, sizeof(action_result), device_action, true, "ok",
        "正在呼叫爸爸", "wechat_voip", NULL, "爸爸");
    assert(action_length > 0);
    assert(strstr(action_result, "\"id\":7") != NULL);
    assert(strstr(action_result, "\"ok\":true") != NULL);
    assert(strstr(action_result, "\"contact_type\":\"wechat_voip\"") != NULL);
    assert(strstr(action_result, "\"matched_name\":\"爸爸\"") != NULL);
    const char string_id_action[] =
        "{\"jsonrpc\":\"2.0\",\"id\":\"tool-9\","
        "\"method\":\"device_action\",\"params\":{}}";
    action_length = xiaotai_ai_encode_call_action_result(
        action_result, sizeof(action_result), string_id_action, false,
        "not_found", "未找到联系人", NULL, NULL, NULL);
    assert(action_length > 0);
    assert(strstr(action_result, "\"id\":\"tool-9\"") != NULL);
    assert(strstr(action_result, "\"code\":-32012") != NULL);
    assert(strstr(action_result, "\"ok\":false") != NULL);
    assert(xiaotai_ai_encode_call_action_result(
               action_result, sizeof(action_result), call, true, "ok", "ok",
               "device_call", "dev-1", "客厅") == 0);
    assert(xiaotai_ai_encode_call_action_result(
               action_result, 8U, device_action, true, "ok", "ok",
               "device_call", "dev-1", "客厅") == -1);
    assert(xiaotai_ai_decode_message(end, strlen(end), "req-1") ==
           XIAOTAI_AI_MESSAGE_END);
    assert(xiaotai_ai_accepts_command(0x2100U, 0x8000U));
    assert(xiaotai_ai_accepts_command(0xa100U, 0x8000U));
    assert(!xiaotai_ai_accepts_command(0x2200U, 0x8000U));

    xiaotai_ai_end_drain_t drain = {0};
    xiaotai_ai_end_drain_begin(&drain, 7U, 1000U, 1500U, 5000U);
    assert(drain.pending);
    assert(xiaotai_ai_end_drain_accepts_remote_close(&drain, 7U));
    assert(!xiaotai_ai_end_drain_accepts_remote_close(&drain, 6U));
    assert(xiaotai_ai_end_drain_step(&drain, 7U, 1100U, false) ==
           XIAOTAI_AI_END_DRAIN_WAIT);
    assert(xiaotai_ai_end_drain_step(&drain, 6U, 1200U, true) ==
           XIAOTAI_AI_END_DRAIN_STALE);
    /* A temporarily empty decoder/DMA queue does not prove that the final
     * TTS packets have arrived.  Keep the transport alive through the bounded
     * arrival grace period even when playback currently reports drained. */
    assert(xiaotai_ai_end_drain_step(&drain, 7U, 2499U, true) ==
           XIAOTAI_AI_END_DRAIN_WAIT);
    assert(xiaotai_ai_end_drain_step(&drain, 7U, 2500U, true) ==
           XIAOTAI_AI_END_DRAIN_DISCONNECT);
    assert(!drain.pending);
    assert(xiaotai_ai_end_drain_accepts_remote_close(&drain, 7U));

    xiaotai_ai_end_drain_begin(&drain, 8U, UINT32_MAX - 100U, 50U, 200U);
    assert(xiaotai_ai_end_drain_step(&drain, 8U, 100U, false) ==
           XIAOTAI_AI_END_DRAIN_DISCONNECT);
    xiaotai_ai_end_drain_cancel(&drain);
    assert(!drain.pending);
    assert(!xiaotai_ai_end_drain_accepts_remote_close(&drain, 8U));

    /* A server may close an established AI transport without first sending
     * end_session.  That is a normal completed conversation after media has
     * become active, but remains an error while the connection is starting. */
    assert(xiaotai_ai_remote_close_is_normal(&drain, 8U, true));
    assert(!xiaotai_ai_remote_close_is_normal(&drain, 8U, false));
    xiaotai_ai_end_drain_begin(&drain, 9U, 0U, 1500U, 5000U);
    assert(xiaotai_ai_remote_close_is_normal(&drain, 9U, false));
    assert(!xiaotai_ai_remote_close_is_normal(&drain, 8U, false));
    return 0;
}
