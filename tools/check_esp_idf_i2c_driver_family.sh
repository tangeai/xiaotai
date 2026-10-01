#!/usr/bin/env bash
set -euo pipefail

elf_path="${1:-build/lckfb_esp32s3_xiaotai.elf}"
nm_bin="${CROSS_NM:-xtensa-esp32s3-elf-nm}"
map_path="${elf_path%.elf}.map"

if [[ ! -f "$elf_path" ]]; then
    echo "FAIL: ELF not found: $elf_path" >&2
    exit 2
fi

if ! command -v "$nm_bin" >/dev/null 2>&1; then
    echo "FAIL: symbol reader not found: $nm_bin" >&2
    exit 2
fi

symbols="$($nm_bin -C "$elf_path")"
has_legacy=0
has_new=0

if grep -Eq '[[:space:]]i2c_driver_install$' <<<"$symbols"; then
    has_legacy=1
fi
if grep -Eq '[[:space:]]i2c_new_master_bus$' <<<"$symbols"; then
    has_new=1
fi

if (( has_legacy && has_new )); then
    echo "FAIL: ELF links both legacy I2C and driver_ng; firmware will abort before app_main" >&2
    exit 1
fi

# Some ESP-IDF components pull the legacy driver in through a global
# constructor.  Link-time garbage collection can remove i2c_driver_install
# itself, so an ELF-symbol-only check would falsely report a driver_ng-only
# image even though the legacy constructor still aborts at boot.
if [[ -f "$map_path" ]] && grep -Eq 'driver/libdriver\.a\(i2c\.c\.obj\)|esp_lcd_panel_io_i2c_v1\.c\.obj' "$map_path"; then
    echo "FAIL: link map contains a legacy I2C object; firmware will abort before app_main" >&2
    exit 1
fi

if (( has_legacy )); then
    echo "PASS: ELF uses legacy I2C only"
elif (( has_new )); then
    echo "PASS: ELF uses driver_ng only"
else
    echo "PASS: ELF links neither I2C driver family"
fi
