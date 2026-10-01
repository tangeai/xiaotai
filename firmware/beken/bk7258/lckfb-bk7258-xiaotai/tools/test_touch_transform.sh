#!/usr/bin/env bash
set -euo pipefail

root_dir="$(cd "$(dirname "$0")/.." && pwd)"
out_file="${TMPDIR:-/tmp}/xiaotai-test-touch-transform"
cc -std=c11 -Wall -Wextra -Werror \
  -I"$root_dir/ap/include" \
  "$root_dir/tools/test_touch_transform.c" \
  "$root_dir/ap/src/xiaotai_touch_transform.c" \
  -o "$out_file"
"$out_file"
