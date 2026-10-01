#!/usr/bin/env bash
set -euo pipefail

project_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
. "$project_dir/tools/repository.sh"
repo_dir="$(xiaotai_find_repository_root "$project_dir")"
media_source="$project_dir/components/starter_media/src/starter_media.c"
voice_source="$repo_dir/platforms/esp-idf/components/starter_voice/src/starter_voice.c"
camera_after_encode="$(sed -n '/bool converted = frame2jpg_cb/,/^}/p' "$media_source")"
# Keep this extractor tied to the capture task rather than an implementation
# variable name.  The codec-adapter migration renamed `filled` to
# `capture_bytes`; the cooperative-delay requirement itself is unchanged.
audio_after_aec="$(sed -n '/static void audio_capture_task/,/static bool play_audio_item/p' "$media_source")"
media_compact="$(tr -d '[:space:]' < "$media_source")"
failures=0

if ! grep -q 'vTaskDelay(pdMS_TO_TICKS(MEDIA_CPU_YIELD_MS))' \
        <<<"$camera_after_encode"; then
    echo "FAIL: successful software JPEG frames can run back-to-back without blocking, starving IDLE/rtc_thread" >&2
    failures=$((failures + 1))
fi

if ! grep -q 'vTaskDelay(pdMS_TO_TICKS(MEDIA_CPU_YIELD_MS))' \
        <<<"$audio_after_aec"; then
    echo "FAIL: backlogged AEC capture can process continuously without blocking, starving IDLE/rtc_thread" >&2
    failures=$((failures + 1))
fi

if ! grep -Fq 'vTaskDelay(pdMS_TO_TICKS(20))' "$voice_source"; then
    echo "FAIL: dedicated wake inference must block between snapshots" >&2
    failures=$((failures + 1))
fi

if rg -q 'esp_task_wdt_(delete|deinit)|CONFIG_ESP_TASK_WDT_CHECK_IDLE_TASK_CPU[01]=n' \
        "$media_source" "$voice_source" "$project_dir/sdkconfig.defaults"; then
    echo "FAIL: media must restore CPU fairness instead of disabling idle-task watchdog coverage" >&2
    failures=$((failures + 1))
fi

if [[ "$media_compact" != *'#defineMEDIA_REALTIME_CORE1'* ||
      "$media_compact" != *'xTaskCreatePinnedToCoreWithCaps(audio_capture_task,"board_audio_tx",6144,NULL,7,NULL,MEDIA_REALTIME_CORE,MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT)'* ||
      "$media_compact" != *'xTaskCreatePinnedToCoreWithCaps(camera_task,"board_mjpeg",8192,NULL,5,NULL,MEDIA_REALTIME_CORE,MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT)'* ]]; then
    echo "FAIL: AEC and software JPEG tasks must be deterministically pinned away from CPU0 Wi-Fi work" >&2
    failures=$((failures + 1))
fi

if [[ "$media_compact" != *'update_max_counter(&s_aec_max_process_us,aec_elapsed_us)'* ||
      "$media_compact" != *'update_max_counter(&s_jpeg_max_encode_us,jpeg_elapsed_us)'* ]]; then
    echo "FAIL: AEC/JPEG deadline telemetry is required for artifact-bound HIL diagnosis" >&2
    failures=$((failures + 1))
fi

if ((failures != 0)); then
    exit 1
fi

echo "PASS: JPEG and AEC loops block cooperatively while both idle-task watchdogs remain protected"
