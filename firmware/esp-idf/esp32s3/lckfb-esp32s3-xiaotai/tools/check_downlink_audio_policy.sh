#!/usr/bin/env bash
set -euo pipefail

project_root=${1:-.}
. "$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)/repository.sh"
repo_dir="$(xiaotai_find_repository_root "$project_root")"
media="$project_root/components/starter_media/src/starter_media.c"
tirtc="$repo_dir/platforms/esp-idf/components/starter_tirtc/src/starter_tirtc.c"
console="$repo_dir/platforms/esp-idf/components/starter_console/src/starter_console.c"

fail() {
    echo "downlink audio policy failed: $1" >&2
    exit 1
}

require_text() {
    local text=$1
    local file=$2
    local reason=$3
    rg -Fq "$text" "$file" || fail "$reason"
}

require_text 'downlink accepted mode=' "$tirtc" \
    'SDK callback must expose the accepted downlink frame contract during HIL'
require_text 'downlink rejected mode=' "$tirtc" \
    'SDK callback must expose rejected stream/media/flag combinations during HIL'
require_text '不能把本端的发送流号当成' "$tirtc" \
    'downlink callback must not assume its outbound stream id matches peer media'
if rg -q 'frame->stream_id == CALL_AUDIO_STREAM' "$tirtc"; then
    fail 'downlink policy must not require CALL_AUDIO_STREAM'
fi
require_text 's_audio_decoded' "$media" \
    'media status must distinguish queued frames from successful A-law decode'
require_text 's_audio_played' "$media" \
    'media status must distinguish successful I2S playback from decode'
require_text 's_audio_playback_blocked' "$media" \
    'media status must expose amp/mute/session playback gates'
require_text 's_audio_write_failed' "$media" \
    'media status must expose I2S write failures'
require_text 's_pcm8k_cancel_sequence' "$media" \
    'realtime media must be able to preempt non-realtime ringtone playback'
require_text 'PCM8k prompt interrupted for realtime media' "$media" \
    'ringtone preemption must be observable during device testing'
require_text 'Downlink: decoded=' "$console" \
    'development status must print the downlink diagnostic counters'

echo 'downlink audio policy passed'
