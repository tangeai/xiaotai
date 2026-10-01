#!/usr/bin/env bash
set -euo pipefail

root_dir="$(cd "$(dirname "$0")/.." && pwd)"
out_file="${TMPDIR:-/tmp}/xiaotai-test-pcm-stream"
cc -std=c11 -Wall -Wextra -Werror \
  -I"$root_dir/ap/include" \
  "$root_dir/tools/test_pcm_stream.c" \
  "$root_dir/ap/src/xiaotai_pcm_stream.c" \
  -o "$out_file"
"$out_file"
