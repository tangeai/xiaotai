#!/usr/bin/env bash
set -euo pipefail
project_dir=$(cd "$(dirname "$0")/.." && pwd)
repo_dir=$(cd "$project_dir/../../../.." && pwd)
if [ "$#" -ne 0 ]; then
    echo "MGKFB has one firmware configuration; do not pass a variant" >&2
    exit 2
fi
exec bash "$repo_dir/platforms/beken/tools/build.sh" "$project_dir"
