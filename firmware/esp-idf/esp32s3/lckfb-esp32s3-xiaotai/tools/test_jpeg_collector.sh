#!/usr/bin/env bash
set -euo pipefail

project_root="$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)"
test_dir="$(mktemp -d)"
trap 'rm -rf "$test_dir"' EXIT
test_binary="$test_dir/jpeg-collector-test"

cc -std=c11 -Wall -Wextra -Werror \
    -I"${project_root}/components/starter_media/src" \
    "${project_root}/components/starter_media/src/starter_jpeg_collector.c" \
    "${project_root}/components/starter_media/test/test_jpeg_collector.c" \
    -o "${test_binary}"

"${test_binary}"
