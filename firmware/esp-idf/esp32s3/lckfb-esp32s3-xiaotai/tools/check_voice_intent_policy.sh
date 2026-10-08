#!/usr/bin/env bash
set -euo pipefail
root="${1:?usage: check_voice_intent_policy.sh <project-root>}"
. "$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)/repository.sh"
repo_dir="$(xiaotai_find_repository_root "$root")"
voice_common="$repo_dir/platforms/esp-idf/components/starter_voice"
voice="$voice_common/src/starter_voice.c"
backend="$voice_common/src/wake_backend.cpp"
if rg -n 'esp_mn_|s_multinet|esp_srmodel_init|recognizer_start' \
    "$repo_dir/platforms/esp-idf/main/app_main.c" "$voice_common/src" ||
   test -e "$root/components/starter_voice/assets/multinet7/srmodels.bin"; then
    echo 'FAIL: MultiNet must not return with dedicated wake integration' >&2
    exit 1
fi
rg -Fq 'wake_window_snapshot' "$voice"
rg -Fq 'wake_result_check' "$voice"
rg -Fq 'epoch != current_epoch' "$voice_common/src/wake_window.h"
rg -Fq 's_cooldown_ms = now + 2500' "$voice"
rg -Fq 'xSemaphoreTake(s_mutex, 0)' "$voice"
rg -Fq 'MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT' "$backend"
rg -Fq 'tflite::VerifyModelBuffer' "$backend"
rg -Fq 'kws_postprocess' "$backend"
rg -Fq 'starter_voice_feed_pcm16k(clean.pcm_16k' "$root/components/starter_media/src/starter_media.c"
rg -Fq 'starter_runtime_main_key' "$repo_dir/platforms/esp-idf/components/starter_button/src/starter_button.c"
if rg -n 'i2s_|TiRtc|starter_runtime_|starter_product_' "$backend"; then
    echo 'FAIL: inference backend must not own audio hardware or sessions' >&2
    exit 1
fi
echo 'PASS: dedicated Voicute wake, bounded latest window, stale-result rejection, no MultiNet'
