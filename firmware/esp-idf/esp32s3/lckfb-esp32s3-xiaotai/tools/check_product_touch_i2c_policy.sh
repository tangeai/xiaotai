#!/usr/bin/env bash
set -euo pipefail

project_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
. "$project_dir/tools/repository.sh"
repo_dir="$(xiaotai_find_repository_root "$project_dir")"
source_path="${1:-$repo_dir/platforms/esp-idf/components/starter_product/src/starter_product.c}"
board_source="$repo_dir/boards/lckfb/esp32s3/board_display.c"

if [[ ! -f "$source_path" ]]; then
    echo "FAIL: product UI source not found: $source_path" >&2
    exit 2
fi

# The touch controller must reuse the board's one driver_ng I2C0 bus. Passing a
# numeric port selects the legacy panel-IO v1 path and crashes in global ctors.
if ! grep -Fq 'szpi_board_i2c_bus()' "$board_source"; then
    echo "FAIL: FT5x06 must reuse the board I2C bus" >&2
    exit 1
fi

if ! grep -Eq 'touch_io_config\.scl_speed_hz[[:space:]]*=[[:space:]]*TOUCH_I2C_HZ;' "$board_source"; then
    echo "FAIL: driver_ng touch panel IO must configure its I2C speed" >&2
    exit 1
fi

if grep -Eq '\(uint32_t\)[[:space:]]*TOUCH_I2C_PORT|driver/i2c\.h' "$source_path" "$board_source"; then
    echo "FAIL: product touch must not retain legacy I2C ownership" >&2
    exit 1
fi

if ! grep -Fq 'szpi_board_display_init(&handles)' "$source_path"; then
    echo "FAIL: product UI must consume the selected board display" >&2
    exit 1
fi

echo "PASS: FT5x06 reuses the board driver_ng I2C0 master"
