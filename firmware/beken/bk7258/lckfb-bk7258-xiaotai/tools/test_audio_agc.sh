#!/usr/bin/env bash
set -euo pipefail

project_dir="$(cd "$(dirname "$0")/.." && pwd)"
source "$project_dir/tools/repository.sh"
repo_dir=$(xiaotai_find_repository_root "$project_dir")
output="$(mktemp)"
trap 'rm -f "$output"' EXIT

cc -std=c11 -Wall -Wextra -Werror \
    -I "$project_dir/tools/fakes" \
    -I "$repo_dir/platforms/beken/runtime/include" \
    "$repo_dir/platforms/beken/runtime/src/xiaotai_audio_agc.c" \
    "$project_dir/tools/test_audio_agc.c" \
    -o "$output"
"$output"
