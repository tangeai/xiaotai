#!/usr/bin/env bash
set -euo pipefail

project_dir="$(cd "$(dirname "$0")/.." && pwd)"
output="$(mktemp)"
trap 'rm -f "$output"' EXIT

cc -std=c11 -Wall -Wextra -Werror \
    -I "$project_dir/ap/include" \
    "$project_dir/ap/src/xiaotai_audio_policy.c" \
    "$project_dir/tools/test_audio_policy.c" \
    -o "$output"
"$output"
