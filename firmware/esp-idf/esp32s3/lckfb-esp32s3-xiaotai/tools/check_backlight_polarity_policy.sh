#!/usr/bin/env bash
set -euo pipefail

project_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
. "$project_dir/tools/repository.sh"
repo_dir="$(xiaotai_find_repository_root "$project_dir")"
source_path="${1:-$repo_dir/platforms/esp-idf/components/starter_product/src/starter_product.c}"
board_config="$repo_dir/boards/lckfb/esp32s3/board_config.h"
board_source="$repo_dir/boards/lckfb/esp32s3/board_display.c"

if [[ ! -f "$source_path" ]]; then
    echo "FAIL: product UI source not found: $source_path" >&2
    exit 2
fi

# On board revision V1.0.1 GPIO42 drives the gate of a SI2301C P-channel
# high-side switch. Low enables BL_A; high disables it. Keep this binary
# output as a GPIO: the product has no brightness control requirement, and a
# fixed level avoids LEDC update/latch ambiguity at the display boot boundary.
if ! rg -Fq '#define LCD_BACKLIGHT_ON_LEVEL 0' "$board_config"; then
    echo 'FAIL: LCD backlight ON must drive the active-low gate low' >&2
    exit 1
fi

if ! rg -Fq '#define LCD_BACKLIGHT_OFF_LEVEL 1' "$board_config"; then
    echo 'FAIL: LCD backlight OFF must drive the active-low gate high' >&2
    exit 1
fi

if ! rg -Fq 'awake ? LCD_BACKLIGHT_ON_LEVEL : LCD_BACKLIGHT_OFF_LEVEL' "$board_source" ||
   ! rg -Fq 'szpi_board_display_set_awake(awake)' "$source_path"; then
    echo 'FAIL: backlight state does not use the board polarity constants' >&2
    exit 1
fi

if rg -Fq 'ledc_' "$source_path" "$board_source"; then
    echo 'FAIL: binary LCD backlight state must not depend on LEDC PWM' >&2
    exit 1
fi

echo 'PASS: GPIO42 fixed-level drive matches the active-low board schematic'
