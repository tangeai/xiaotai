#!/usr/bin/env bash
set -euo pipefail

root_dir="$(cd "$(dirname "$0")/.." && pwd)"
common_repo_dir=$(cd "$root_dir/../../../.." && pwd)
out_file="${TMPDIR:-/tmp}/xiaotai-test-pcm-stream"
cc -std=c11 -Wall -Wextra -Werror \
  -I"$common_repo_dir/platforms/beken/runtime/include" \
  "$root_dir/tools/test_pcm_stream.c" \
  "$common_repo_dir/platforms/beken/runtime/src/xiaotai_pcm_stream.c" \
  -o "$out_file"
"$out_file"
