#!/usr/bin/env sh
set -eu

repo_dir=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
binary="${TMPDIR:-/tmp}/xiaotai-ui-hit-test"

cc -std=c11 -Wall -Wextra -Werror \
    -I"$repo_dir/ap/include" \
    "$repo_dir/tools/test_ui_hit_test.c" \
    "$repo_dir/ap/src/xiaotai_ui_hit_test.c" \
    -o "$binary"
"$binary"
