#!/usr/bin/env bash
set -euo pipefail

project_dir=${1:-.}
elf=${2:-}
cmake_file="$project_dir/components/tirtc_sdk/CMakeLists.txt"
policy_source="$project_dir/components/tirtc_sdk/src/tirtc_task_stack_policy.c"

fail() {
    echo "TiRTC thread stack policy failed: $1" >&2
    exit 1
}

rg -Fq 'src/tirtc_task_stack_policy.c' "$cmake_file" ||
    fail 'the TiRTC stack policy source is not compiled'
rg -Fq -- '--wrap=freertos_ThreadCreateWithStackSize' "$cmake_file" ||
    fail 'the TiRTC task-create wrapper is not linked'
[[ -f "$policy_source" ]] || fail 'the TiRTC stack policy source is missing'
rg -Fq 'TIRTC_RTC_THREAD_MIN_STACK_BYTES (24 * 1024)' "$policy_source" ||
    fail 'rtc_thread stack budget is not 24 KiB'
rg -Fq '__wrap_freertos_ThreadCreateWithStackSize' "$policy_source" ||
    fail 'the TiRTC task-create wrapper implementation is missing'

if [[ -n "$elf" ]]; then
    [[ -s "$elf" ]] || fail 'linked ELF is missing'
    "${CROSS_NM:-nm}" -g "$elf" | rg -q '__wrap_freertos_ThreadCreateWithStackSize' ||
        fail 'linked ELF does not contain the TiRTC task-create wrapper'
fi

echo 'TiRTC thread stack policy passed: rtc_thread minimum stack=24576 bytes'
