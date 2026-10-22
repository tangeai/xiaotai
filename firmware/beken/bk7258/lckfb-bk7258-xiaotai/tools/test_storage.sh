#!/usr/bin/env bash
set -euo pipefail

root_dir="$(cd "$(dirname "$0")/.." && pwd)"
common_repo_dir=$(cd "$root_dir/../../../.." && pwd)
out_file="${TMPDIR:-/tmp}/xiaotai-test-storage"
cc -std=c11 -Wall -Wextra -Werror \
  -I"$root_dir/tools/fakes" -I"$root_dir/tools/fakes/common" \
  -I"$common_repo_dir/platforms/beken/runtime/include" \
  "$root_dir/tools/test_storage.c" \
  "$common_repo_dir/platforms/beken/runtime/src/xiaotai_storage.c" \
  -o "$out_file"
"$out_file"
