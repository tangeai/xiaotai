#!/usr/bin/env bash
set -euo pipefail

repository_root=$(cd "$(dirname "$0")/.." && pwd)
tools_dir="$repository_root/.tools"
idf_dir="$tools_dir/esp-idf-v5.5.4"
idf_tools_dir="$tools_dir/espressif-v5.5.4"
idf_repository="https://github.com/espressif/esp-idf.git"
idf_tag="v5.5.4"
idf_commit="735507283d5b2f9fb363a1901172dbd9e847945d"

target="${1:-}"
case "$target" in
    esp32s3|esp32p4)
        install_targets="$target"
        ;;
    all)
        install_targets="esp32s3,esp32p4"
        ;;
    *)
        echo "Usage: $0 {esp32s3|esp32p4|all}" >&2
        exit 2
        ;;
esac

mkdir -p "$tools_dir"
if [ ! -d "$idf_dir/.git" ]; then
    git clone --recursive --branch "$idf_tag" "$idf_repository" "$idf_dir"
fi

actual_commit=$(git -C "$idf_dir" rev-parse HEAD)
if [ "$actual_commit" != "$idf_commit" ]; then
    echo "ESP-IDF identity mismatch: expected $idf_commit, got $actual_commit" >&2
    exit 1
fi

git -C "$idf_dir" submodule update --init --recursive
IDF_TOOLS_PATH="$idf_tools_dir" "$idf_dir/install.sh" "$install_targets"

echo "ESP-IDF ready: $idf_tag ($actual_commit)"
echo "Installed target: $install_targets"
echo "Activate it with: . tools/activate_esp_idf.sh"
