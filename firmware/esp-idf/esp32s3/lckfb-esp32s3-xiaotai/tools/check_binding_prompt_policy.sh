#!/usr/bin/env bash
set -euo pipefail

project_root=${1:-$(cd "$(dirname "$0")/.." && pwd)}
. "$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)/repository.sh"
repo_dir="$(xiaotai_find_repository_root "$project_root")"
platform_source="$repo_dir/platforms/esp-idf/components/platform_client/src/platform_client.c"
product_source="$repo_dir/platforms/esp-idf/components/starter_product/src/starter_product.c"
media_source="$project_root/components/starter_media/src/starter_media.c"
main_source="$repo_dir/platforms/esp-idf/main/app_main.c"

require_text() {
    local text=$1
    local file=$2
    local reason=$3
    if ! rg -Fq "$text" "$file"; then
        echo "binding prompt policy failed: $reason" >&2
        exit 1
    fi
}

require_text '/v1/device/tts?code=%s' "$platform_source" \
    'firmware must request the server-generated prompt for the current code'
require_text 'report->temp_token' "$platform_source" \
    'TTS must use the temporary token from the same device report'
require_text 'MQTT_EVENT_SUBSCRIBED' "$platform_source" \
    'temporary MQTT must be subscribed before prompt playback'
require_text 'platform_client_verification_code()' "$product_source" \
    'product UI must read the transient verification code'
require_text 'PAGE_BINDING' "$product_source" \
    'product UI must own a dedicated binding page'
require_text 'xiaotai_verification_code_draw' "$product_source" \
    'verification code must use the shared geometric digit renderer'
require_text 'lv_obj_add_event_cb(code_panel, binding_code_draw, LV_EVENT_DRAW_MAIN' "$product_source" \
    'verification digits must draw at actual panel coordinates without label clipping'
require_text 'starter_media_play_pcm8k' "$media_source" \
    'verification PCM must use the board media output path'
require_text 'VERIFICATION_PROMPT_REPEAT_COUNT 3U' "$main_source" \
    'product verification prompt must repeat exactly three times'

if rg -Fq 'verification code: %s' "$platform_source"; then
    echo 'binding prompt policy failed: verification code must not be logged' >&2
    exit 1
fi

echo 'binding prompt policy passed'
