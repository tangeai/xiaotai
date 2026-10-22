#!/usr/bin/env bash
set -euo pipefail
project_dir=$(cd "$(dirname "$0")/.." && pwd)
repo_dir=$(cd "$project_dir/../../../.." && pwd)
exec bash "$repo_dir/platforms/beken/tools/build.sh" "$project_dir"
