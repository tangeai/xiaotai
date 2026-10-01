#!/usr/bin/env bash
set -euo pipefail

project_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
. "$project_dir/tools/repository.sh"
repo_dir="$(xiaotai_find_repository_root "$project_dir")"
exec bash "$repo_dir/tools/run_host_tests.sh" "$@"
