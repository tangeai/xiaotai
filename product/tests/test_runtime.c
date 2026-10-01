#include <assert.h>
#include <stddef.h>

#include "xiaotai_runtime.h"

int main(void)
{
    xiaotai_runtime_t runtime;
    xiaotai_runtime_init(&runtime);
    assert(runtime.state == XIAOTAI_STATE_WAITING);
    assert(!xiaotai_runtime_begin_ending(NULL, 1U, 0U, 1U));
    assert(!xiaotai_runtime_begin_ending(&runtime, 0U, 0U, 1U));
    assert(!xiaotai_runtime_begin(&runtime,
                                  (xiaotai_session_owner_t)99, false));
    assert(runtime.owner == XIAOTAI_OWNER_NONE);
    assert(runtime.generation == 0U);
    assert(xiaotai_runtime_begin(&runtime, XIAOTAI_OWNER_AI, false));
    uint32_t generation = runtime.generation;
    assert(!xiaotai_runtime_begin_ending(&runtime, generation, 0U, 1U));
    assert(xiaotai_runtime_arm_timeout(&runtime, generation, 1000, 10000));
    assert(!xiaotai_runtime_expire(&runtime, 10999, NULL, NULL, NULL));
    assert(!xiaotai_runtime_begin(&runtime, XIAOTAI_OWNER_WECHAT_VOIP, false));
    assert(!xiaotai_runtime_media_started(&runtime, generation + 1));
    assert(xiaotai_runtime_media_started(&runtime, generation));
    assert(runtime.state == XIAOTAI_STATE_AI_ACTIVE);
    assert(xiaotai_runtime_finish(&runtime, generation));

    assert(xiaotai_runtime_begin(&runtime, XIAOTAI_OWNER_WECHAT_VOIP, true));
    generation = runtime.generation;
    assert(!xiaotai_runtime_begin_ending(&runtime, generation, 0U, 0U));
    assert(!xiaotai_runtime_begin_ending(&runtime, generation, 0U, 1U));
    assert(xiaotai_runtime_arm_timeout(&runtime, generation,
                                       0xfffffff0U, 32U));
    assert(!xiaotai_runtime_expire(&runtime, 0x0fU, NULL, NULL, NULL));
    xiaotai_session_owner_t owner = XIAOTAI_OWNER_NONE;
    xiaotai_runtime_state_t state = XIAOTAI_STATE_WAITING;
    uint32_t expired_generation = 0;
    assert(xiaotai_runtime_expire(&runtime, 0x10U, &owner, &state,
                                  &expired_generation));
    assert(owner == XIAOTAI_OWNER_WECHAT_VOIP);
    assert(state == XIAOTAI_STATE_CALL_INCOMING);
    assert(expired_generation == generation);

    assert(xiaotai_runtime_begin(&runtime, XIAOTAI_OWNER_AI, false));
    generation = runtime.generation;
    assert(xiaotai_runtime_offer_incoming(&runtime,
        XIAOTAI_OWNER_DEVICE_CALL, 100U, 45000U));
    assert(runtime.owner == XIAOTAI_OWNER_AI);
    assert(xiaotai_runtime_has_incoming(&runtime));
    assert(!xiaotai_runtime_offer_incoming(&runtime,
        XIAOTAI_OWNER_WECHAT_VOIP, 101U, 45000U));
    assert(!xiaotai_runtime_accept_incoming(&runtime, NULL, NULL));
    assert(xiaotai_runtime_finish(&runtime, generation));
    assert(xiaotai_runtime_accept_incoming(&runtime, &owner, &generation));
    assert(owner == XIAOTAI_OWNER_DEVICE_CALL);
    assert(runtime.state == XIAOTAI_STATE_CALL_CONNECTING);
    assert(xiaotai_runtime_begin_ending(&runtime, generation,
                                         UINT32_MAX, 1U));
    assert(runtime.deadline_ms == 1U);
    runtime.state = XIAOTAI_STATE_CALL_CONNECTING;
    assert(xiaotai_runtime_begin_ending(&runtime, generation, 200U, 2000U));
    assert(runtime.state == XIAOTAI_STATE_CALL_ENDING);
    assert(runtime.deadline_ms == 2200U);
    assert(!xiaotai_runtime_media_started(&runtime, generation));
    assert(!xiaotai_runtime_begin_ending(&runtime, generation + 1U,
                                         300U, 2000U));
    assert(!xiaotai_runtime_remote_view_allowed(&runtime, false));
    assert(xiaotai_runtime_finish(&runtime, generation));
    assert(xiaotai_runtime_remote_view_allowed(&runtime, false));
    assert(!xiaotai_runtime_remote_view_allowed(&runtime, true));

    assert(xiaotai_runtime_begin(&runtime, XIAOTAI_OWNER_ROOM, false));
    generation = runtime.generation;
    assert(runtime.state == XIAOTAI_STATE_ROOM_CONNECTING);
    assert(xiaotai_runtime_media_started(&runtime, generation));
    assert(runtime.state == XIAOTAI_STATE_ROOM_ACTIVE);
    assert(xiaotai_runtime_finish(&runtime, generation));

    assert(xiaotai_runtime_offer_incoming(&runtime,
        XIAOTAI_OWNER_WECHAT_VOIP, 0xfffffff0U, 32U));
    assert(!xiaotai_runtime_home_allowed(&runtime));
    assert(!xiaotai_runtime_expire_incoming(&runtime, 0x0fU, NULL));
    assert(xiaotai_runtime_expire_incoming(&runtime, 0x10U, &owner));
    assert(owner == XIAOTAI_OWNER_WECHAT_VOIP);
    assert(!xiaotai_runtime_has_incoming(&runtime));
    assert(xiaotai_runtime_home_allowed(&runtime));
    return 0;
}
