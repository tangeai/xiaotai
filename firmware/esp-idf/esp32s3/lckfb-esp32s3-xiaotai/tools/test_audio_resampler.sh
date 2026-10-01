#!/usr/bin/env bash
set -euo pipefail

project_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
. "$project_dir/tools/repository.sh"
repo_dir="$(xiaotai_find_repository_root "$project_dir")"
tmp_dir="$(mktemp -d)"
trap 'rm -rf "$tmp_dir"' EXIT

cc -std=c11 -Wall -Wextra -Werror \
   -I"$repo_dir/platforms/esp-idf/components/starter_media_common/include" \
   "$project_dir/tools/test_audio_resampler.c" \
   "$repo_dir/platforms/esp-idf/components/starter_media_common/src/starter_audio_resampler.c" \
   -o "$tmp_dir/test_audio_resampler"
"$tmp_dir/test_audio_resampler"
