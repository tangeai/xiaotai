#!/usr/bin/env bash
set -euo pipefail

project_dir=${1:-.}
. "$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)/repository.sh"
repo_dir="$(xiaotai_find_repository_root "$project_dir")"
runtime="$repo_dir/platforms/esp-idf/components/starter_runtime/src/starter_runtime.c"
main_source="$repo_dir/platforms/esp-idf/main/app_main.c"
platform="$repo_dir/platforms/esp-idf/components/platform_client/src/platform_client.c"

fail() {
    echo "realtime connect memory policy failed: $1" >&2
    exit 1
}

[[ $(rg -c 'starter_tirtc_(ai|voip|call|room)_connect\(' "$runtime") -eq 4 ]] ||
    fail 'unexpected number of TiRTC external-connect call sites'
[[ $(rg -c 'if \(!suspend_mqtt_for_external_connect\(\)\)' "$runtime") -eq 4 ]] ||
    fail 'every TiRTC external connect must first suspend MQTT'
rg -Fq 'VOIP_CONNECT_TASK_STACK_BYTES (24U * 1024U)' "$runtime" ||
    fail 'VoIP WHIP must run on its dedicated 24 KiB worker stack'
rg -Fq 'xTaskCreateWithCaps(voip_connect_task' "$runtime" ||
    fail 'VoIP WHIP must not block the session state machine'
rg -Fq 'resume_mqtt_after_external_connect();' "$runtime" ||
    fail 'MQTT must resume after the asynchronous connection callback'
rg -Fq 'esp_mqtt_client_stop(mqtt)' "$platform" ||
    fail 'MQTT stop must release its TLS/task allocations'
rg -Fq 'esp_mqtt_client_destroy(mqtt)' "$platform" ||
    fail 'MQTT client must be destroyed before TiRTC requests contiguous heap'
rg -Fq 'EXTERNAL_CONNECT_RESERVE_BYTES (17U * 1024U)' "$runtime" ||
    fail 'TiRTC external connect must reserve a 17 KiB contiguous internal block'
rg -Fq 'starter_runtime_arm_external_connect_reserve();' "$runtime" ||
    fail 'MQTT resume must re-arm the external connect reserve before restarting MQTT'
rg -Fq 'heap_caps_free(s_external_connect_reserve);' "$runtime" ||
    fail 'external connect gate must release the reserved contiguous block after MQTT destroy'
rg -Fq 'starter_runtime_arm_external_connect_reserve() != ESP_OK' "$main_source" ||
    fail 'startup must arm the external-connect reserve before voice and MQTT startup'

echo 'realtime connect memory policy passed'
