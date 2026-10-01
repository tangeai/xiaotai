#!/usr/bin/env bash
set -euo pipefail

project_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
defaults="$project_dir/sdkconfig.defaults"
resolved="$project_dir/sdkconfig"
media_source="$project_dir/components/starter_media/src/starter_media.c"

check_core1() {
    local config_file="$1"
    if ! rg -q '^CONFIG_CAMERA_CORE1=y$' "$config_file"; then
        echo "FAIL: camera event task must run on CPU1, away from the CPU0 Wi-Fi task ($config_file)" >&2
        exit 1
    fi
    if rg -q '^CONFIG_CAMERA_CORE0=y$' "$config_file"; then
        echo "FAIL: camera event task is still pinned to CPU0 with Wi-Fi ($config_file)" >&2
        exit 1
    fi
}

check_core1 "$defaults"
check_core1 "$resolved"

media_compact="$(tr -d '[:space:]' < "$media_source")"
if [[ "$media_compact" != *'#defineCAMERA_TARGET_FPS8U'* ||
      "$media_compact" != *'#defineCAMERA_FRAME_INTERVAL_MS(1000U/CAMERA_TARGET_FPS)'* ]]; then
    echo "FAIL: camera pacing must target 8 fps" >&2
    exit 1
fi

echo "PASS: camera event task is pinned to CPU1, isolated from Wi-Fi, and paced at 8 fps"
