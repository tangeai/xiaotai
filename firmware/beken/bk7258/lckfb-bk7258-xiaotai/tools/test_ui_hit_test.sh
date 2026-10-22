#!/usr/bin/env sh
set -eu

repo_dir=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
common_repo_dir=$(cd "$repo_dir/../../../.." && pwd)
binary="${TMPDIR:-/tmp}/xiaotai-ui-hit-test"

cc -std=c11 -Wall -Wextra -Werror \
    -I"$common_repo_dir/platforms/beken/runtime/include" \
    "$repo_dir/tools/test_ui_hit_test.c" \
    "$common_repo_dir/platforms/beken/runtime/src/xiaotai_ui_hit_test.c" \
    -o "$binary"
"$binary"
