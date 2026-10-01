#!/usr/bin/env bash

if [ "${BASH_SOURCE[0]}" = "$0" ]; then
    echo "This script must be sourced: . tools/activate_esp_idf.sh" >&2
    exit 2
fi

xiaotai_repository_root=$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)
export IDF_TOOLS_PATH="$xiaotai_repository_root/.tools/espressif-v5.5.4"
. "$xiaotai_repository_root/.tools/esp-idf-v5.5.4/export.sh"
unset xiaotai_repository_root
