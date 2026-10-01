#!/usr/bin/env bash
set -euo pipefail

root="${1:?usage: check_session_priority_policy.sh <project-root>}"
. "$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)/repository.sh"
repo_dir="$(xiaotai_find_repository_root "$root")"
runtime="$repo_dir/platforms/esp-idf/components/starter_runtime/src/starter_runtime.c"
product="$repo_dir/platforms/esp-idf/components/starter_product/src/starter_product.c"
voice="$repo_dir/platforms/esp-idf/components/starter_voice/src/starter_voice.c"

require() {
    rg -Fq "$1" "$2" || {
        echo "session priority policy: missing $1 in $2" >&2
        exit 1
    }
}

# Answered Call/VoIP > AI > room > H5. An incoming notification remains pending
# until the user answers; foreground work then suspends the persistent room.
# releasing PTT closes uplink immediately even if the control queue is full.
require 'call preempts foreground owner=%s' "$runtime"
require 'xiaotai_runtime_offer_incoming' "$runtime"
require 's_session.pending_deadline_ms' "$runtime"
require 'starter_tirtc_accept_h5(false);' "$runtime"
require 'room_stop_connection("suspended", 0)' "$runtime"
require 'atomic_store_explicit(&s_room_release_required, true,' "$runtime"
require 'starter_media_set_uplink_enabled(false);' "$runtime"
require '/v1/call/group/device/connect-token' "$runtime"
require '"method\":\"join_room' "$runtime"
require 'STARTER_RUNTIME_ROOM_ACTIVE' "$runtime"
require 'AI session closed by remote/server idle timeout' "$runtime"
require 'AI session ended by remote end_session/idle policy' "$runtime"
require 'wake ignored while foreground state=%s' "$product"
require 'CALL_RING_AI_ACK' "$product"
require '在呢。' "$product"
require 'ai_ack_female_8k_wav_start' "$product"
require 'ai_ack_male_8k_wav_start' "$product"
require 'ack_voice' "$product"
require '回应声  %s' "$product"
require 'wake_result_check' "$voice"
require 's_cooldown_ms = now + 2500' "$voice"
require '#define RUNTIME_TASK_STACK_BYTES 24576U' "$runtime"

echo 'session priority policy passed: incoming stays pending; answered call/VoIP > AI > room > H5, PTT fails closed, session stack headroom guarded'
