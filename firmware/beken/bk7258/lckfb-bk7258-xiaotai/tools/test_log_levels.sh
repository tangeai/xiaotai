#!/usr/bin/env bash
set -euo pipefail

repo_dir=$(cd "$(dirname "$0")/.." && pwd)
development_binary="${TMPDIR:-/tmp}/xiaotai-log-level-development-test"
release_binary="${TMPDIR:-/tmp}/xiaotai-log-level-release-test"

cc -std=c11 -Wall -Wextra -Werror \
    -I"$repo_dir/tools/test_stubs" \
    -I"$repo_dir/ap/include" \
    "$repo_dir/tools/test_log_levels.c" \
    "$repo_dir/ap/src/xiaotai_log.c" \
    -o "$development_binary"
"$development_binary"

cc -std=c11 -Wall -Wextra -Werror -DCONFIG_LOG_LEVEL=3 \
    -I"$repo_dir/tools/test_stubs" \
    -I"$repo_dir/ap/include" \
    "$repo_dir/tools/test_log_levels.c" \
    "$repo_dir/ap/src/xiaotai_log.c" \
    -o "$release_binary"
"$release_binary"
