#!/usr/bin/env bash
set -euo pipefail

if [[ $# -ne 1 ]]; then
    echo "usage: $0 <firmware.elf>" >&2
    exit 2
fi

project_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
. "$project_dir/tools/repository.sh"
repo_dir="$(xiaotai_find_repository_root "$project_dir")"
runtime_source="$repo_dir/platforms/esp-idf/components/starter_runtime/src/starter_runtime.c"
tirtc_source="$repo_dir/platforms/esp-idf/components/starter_tirtc/src/starter_tirtc.c"
elf="$1"
objdump="${CROSS_OBJDUMP:-xtensa-esp32s3-elf-objdump}"
minimum_nested_reserve_bytes=4096
maximum_token_handler_frame_bytes=512
# starter_session is explicitly PSRAM-backed by the memory-placement gate.
# Hardware high-water telemetry reached 428 bytes with 13 KiB, so allow the
# measured 24 KiB safety budget while still rejecting accidental stack growth.
maximum_session_task_stack_bytes=24576

if [[ ! -f "$elf" ]]; then
    echo "FAIL: ELF not found: $elf" >&2
    exit 1
fi
if ! command -v "$objdump" >/dev/null 2>&1; then
    echo "FAIL: objdump not found: $objdump" >&2
    exit 1
fi

tirtc_compact="$(tr -d '[:space:]' < "$tirtc_source")"
if [[ "$tirtc_compact" != *'heap_caps_get_free_size(MALLOC_CAP_INTERNAL|MALLOC_CAP_DMA)'* ||
      "$tirtc_compact" != *'heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL|MALLOC_CAP_DMA)'* ||
      "$tirtc_compact" != *'log_start_memory("pre-init");'* ||
      "$tirtc_compact" != *'log_start_memory("pre-start");rc=TiRtcStart('* ]]; then
    echo "FAIL: TiRTC startup must expose pre-init/pre-start internal DMA heap headroom" >&2
    exit 1
fi

task_stack_bytes="$(sed -nE \
    's/^#define RUNTIME_TASK_STACK_BYTES ([0-9]+)U$/\1/p' \
    "$runtime_source")"
if [[ -z "$task_stack_bytes" ]]; then
    task_stack_bytes="$(sed -nE \
        's/.*xTaskCreate\(runtime_task, "starter_session", ([0-9]+),.*/\1/p' \
        "$runtime_source")"
fi
if [[ -z "$task_stack_bytes" ]]; then
    echo "FAIL: cannot resolve starter_session stack size" >&2
    exit 1
fi

entry_frame_bytes() {
    local symbol="$1"
    local raw entry_hex immediate_high immediate_upper
    raw="$("$objdump" -d --disassemble="$symbol" "$elf" | awk -v name="$symbol" '
        $0 ~ "<" name ">:" { found = 1; next }
        found && $1 ~ /^[[:xdigit:]]+:$/ { print $2; exit }
    ')"
    if [[ ${#raw} -lt 6 ]]; then
        echo "FAIL: cannot read Xtensa entry instruction for $symbol" >&2
        exit 1
    fi
    entry_hex="${raw: -6}"
    if [[ "${entry_hex: -2}" != "36" || "${entry_hex:3:1}" != "1" ]]; then
        echo "FAIL: $symbol does not start with the expected Xtensa entry a1 instruction" >&2
        exit 1
    fi
    immediate_upper="${entry_hex:0:2}"
    immediate_high="${entry_hex:2:1}"
    echo $(( (16#$immediate_high + 16#$immediate_upper * 16) * 8 ))
}

runtime_frame="$(entry_frame_bytes runtime_task)"
token_frame="$(entry_frame_bytes handle_ai_token)"
adapter_frame="$(entry_frame_bytes starter_tirtc_ai_connect)"
whip_frame="$(entry_frame_bytes TiRtcWhipConnect)"
known_chain_bytes=$((runtime_frame + token_frame + adapter_frame + whip_frame))
nested_reserve_bytes=$((task_stack_bytes - known_chain_bytes))

if (( token_frame > maximum_token_handler_frame_bytes )); then
    echo "FAIL: handle_ai_token frame is $token_frame bytes; move peer/token buffers off the task stack" >&2
    exit 1
fi
if (( task_stack_bytes > maximum_session_task_stack_bytes )); then
    echo "FAIL: starter_session stack is $task_stack_bytes bytes; reviewed PSRAM budget is $maximum_session_task_stack_bytes" >&2
    exit 1
fi
if (( nested_reserve_bytes < minimum_nested_reserve_bytes )); then
    echo "FAIL: starter_session stack leaves only $nested_reserve_bytes bytes for nested SDK/RTOS calls; require at least $minimum_nested_reserve_bytes" >&2
    exit 1
fi

echo "PASS: AI/TLS stack budget task=$task_stack_bytes known-chain=$known_chain_bytes nested-reserve=$nested_reserve_bytes token-frame=$token_frame PSRAM-limit=$maximum_session_task_stack_bytes"
