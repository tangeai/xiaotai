#!/usr/bin/env bash
set -euo pipefail
project_dir=$(cd "$(dirname "$0")/.." && pwd)
repo_dir=$(cd "$project_dir/../../../.." && pwd)
variant="${1:-eyes}"
case "$variant" in eyes|headless) ;; *) echo "Unknown MGKFB variant: $variant" >&2; exit 2;; esac
exec bash "$repo_dir/platforms/beken/tools/build.sh" "$project_dir" "$variant"
