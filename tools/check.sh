#!/usr/bin/env bash
set -euo pipefail

repo_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"

bash "$repo_dir/tools/run_host_tests.sh"
python3 "$repo_dir/tools/run_c_sanitizers.py"
python3 "$repo_dir/tools/product_coverage.py"
python3 "$repo_dir/tools/build.py" --validate
git -C "$repo_dir" diff --check

echo "XiaoTai repository checks passed"
