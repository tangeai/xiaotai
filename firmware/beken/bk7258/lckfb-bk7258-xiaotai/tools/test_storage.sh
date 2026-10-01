#!/usr/bin/env bash
set -euo pipefail

root_dir="$(cd "$(dirname "$0")/.." && pwd)"
out_file="${TMPDIR:-/tmp}/xiaotai-test-storage"
cc -std=c11 -Wall -Wextra -Werror \
  -I"$root_dir/tools/fakes" -I"$root_dir/tools/fakes/common" \
  -I"$root_dir/ap/include" \
  "$root_dir/tools/test_storage.c" \
  "$root_dir/ap/src/xiaotai_storage.c" \
  -o "$out_file"
"$out_file"
