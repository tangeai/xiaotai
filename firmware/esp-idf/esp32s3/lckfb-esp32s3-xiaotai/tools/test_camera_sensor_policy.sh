#!/usr/bin/env bash
set -euo pipefail

project_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
temp_dir="$(mktemp -d)"
trap 'rm -rf -- "$temp_dir"' EXIT

"${CC:-cc}" \
    -std=c11 \
    -Wall \
    -Wextra \
    -Werror \
    -I"$project_dir/components/starter_media/include" \
    -I"$project_dir/tools/fixtures/esp32_camera" \
    "$project_dir/tools/test_camera_sensor_policy.c" \
    -o "$temp_dir/test_camera_sensor_policy"

"$temp_dir/test_camera_sensor_policy"
