#!/usr/bin/env bash
set -euo pipefail

project_dir=${1:-.}
. "$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)/repository.sh"
repo_dir="$(xiaotai_find_repository_root "$project_dir")"
main_source="$repo_dir/platforms/esp-idf/main/app_main.c"
tirtc_source="$repo_dir/platforms/esp-idf/components/starter_tirtc/src/starter_tirtc.c"

fail() {
    echo "TiRTC startup order policy failed: $1" >&2
    exit 1
}

tirtc_line=$(rg -n -m1 'starter_tirtc_start\(&tirtc\)' "$main_source" |
    cut -d: -f1)
platform_line=$(rg -n -m1 'platform_client_start\(&platform\)' "$main_source" |
    cut -d: -f1)

[[ -n "$tirtc_line" && -n "$platform_line" ]] ||
    fail 'cannot locate TiRTC/platform bootstrap calls'
((tirtc_line < platform_line)) ||
    fail 'TiRTC must reserve its contiguous internal heap before platform MQTT/TLS'

app_main_body=$(sed -n '/^void app_main(void)/,/^}/p' "$main_source")
if rg -q 'starter_button_start' <<<"$app_main_body"; then
    fail 'button controls must not reserve stacks before TiRTC startup'
fi

# Recovery commands must remain available when networking or TiRTC fails.
# Their task is an intentional early internal-stack allocation.
console_line=$(rg -n -m1 'starter_console_start\(' <<<"$app_main_body" | cut -d: -f1)
media_line=$(rg -n -m1 'starter_media_init\(' <<<"$app_main_body" | cut -d: -f1)
[[ -n "$console_line" && -n "$media_line" ]] || fail 'missing early recovery console'
((console_line < media_line)) || fail 'recovery console must precede media initialization'

rg -Fq 'TIRTC_FIRST_CONTIGUOUS_HEAP'  "$main_source" ||
    fail 'the startup memory invariant must remain documented in source'

rg -Fq 'TIRTC_TGTRP_POLL_TIMEOUT_MS 20' "$tirtc_source" ||
    fail 'SDK transport poll timeout must prevent rtc_thread busy polling'
rg -Fq 'TiRtcSetOption(TIRTC_OPT_TGTRP_POLL_TIMEOUT' "$tirtc_source" ||
    fail 'SDK transport poll timeout must be applied before TiRtcStart'

echo 'TiRTC startup order policy passed'
