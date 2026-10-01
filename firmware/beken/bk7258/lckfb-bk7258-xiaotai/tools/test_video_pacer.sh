#!/usr/bin/env bash
set -euo pipefail

repo_dir="$(cd "$(dirname "$0")/.." && pwd)"
source "$repo_dir/tools/repository.sh"
workspace_dir="$(xiaotai_find_repository_root "$repo_dir")"
binary="${TMPDIR:-/tmp}/xiaotai-video-pacer-test"

cc -std=c11 -Wall -Wextra -Werror \
    -I"$workspace_dir/product/include" \
    "$workspace_dir/product/tests/test_video_pacer.c" \
    "$workspace_dir/product/src/xiaotai_video_pacer.c" \
    -o "$binary"
"$binary"
