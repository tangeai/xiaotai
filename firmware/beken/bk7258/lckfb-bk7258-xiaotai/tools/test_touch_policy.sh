#!/usr/bin/env bash
set -euo pipefail

root_dir="$(cd "$(dirname "$0")/.." && pwd)"
repo_dir="$(cd "$root_dir/../../../.." && pwd)"
out_file="${TMPDIR:-/tmp}/xiaotai-test-touch-policy"
cc -std=c11 -Wall -Wextra -Werror \
  -I"$root_dir/ap/include" -I"$repo_dir/boards/lckfb/bk7258" \
  "$root_dir/tools/test_touch_policy.c" \
  "$root_dir/ap/src/xiaotai_touch_policy.c" \
  "$root_dir/ap/src/xiaotai_touch_transform.c" \
  -o "$out_file"
"$out_file"
