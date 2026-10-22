#!/usr/bin/env sh
set -eu

repo_dir=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
common_repo_dir=$(cd "$repo_dir/../../../.." && pwd)
binary="${TMPDIR:-/tmp}/xiaotai-network-state-test"

cc -std=c11 -Wall -Wextra -Werror \
    -I"$common_repo_dir/platforms/beken/runtime/include" \
    "$repo_dir/tools/test_network_state.c" \
    "$common_repo_dir/platforms/beken/runtime/src/xiaotai_network_state.c" \
    -o "$binary"
"$binary"
