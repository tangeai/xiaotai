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
    -I"$project_dir/components/starter_media/src" \
    "$project_dir/tests/key_gesture_test.c" \
    -o "$temp_dir/key_gesture_test"

"$temp_dir/key_gesture_test"
echo "PASS: ATK key gesture contract"
