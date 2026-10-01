#!/usr/bin/env sh
set -eu

repo_dir=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
binary="${TMPDIR:-/tmp}/xiaotai-network-state-test"

cc -std=c11 -Wall -Wextra -Werror \
    -I"$repo_dir/ap/include" \
    "$repo_dir/tools/test_network_state.c" \
    "$repo_dir/ap/src/xiaotai_network_state.c" \
    -o "$binary"
"$binary"
