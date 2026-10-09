#!/usr/bin/env python3
"""Generate one full CJK LVGL font; no fallback to another pixel size."""
import argparse
from pathlib import Path
import subprocess


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--size', type=int, choices=(16, 24), required=True)
    parser.add_argument('--font', type=Path, required=True,
                        help='Source font with complete basic CJK coverage (e.g. SimHei)')
    parser.add_argument('--converter', default='lv_font_conv',
                        help='lv_font_conv executable')
    args = parser.parse_args()
    root = Path(__file__).resolve().parents[1]
    destination = root / f'platforms/esp-idf/components/starter_product/src/ui_font_cn_{args.size}.c'
    command = [args.converter, '--size', str(args.size), '--bpp', '2',
               '--format', 'lvgl', '--font', str(args.font), '--no-kerning',
               '--lv-include', 'lvgl.h', '--lv-font-name', f'ui_font_cn_{args.size}',
               '--symbols', '°℃', '-o', str(destination)]
    for interval in ('0x20-0x7e', '0x2000-0x206f', '0x2190-0x22ff',
                     '0x2500-0x25ff', '0x3000-0x303f', '0x4e00-0x9fff', '0xff00-0xffef'):
        command.extend(('-r', interval))
    subprocess.run(command, check=True)
    destination.write_text(destination.read_text().rstrip() + '\n')


if __name__ == '__main__':
    main()
