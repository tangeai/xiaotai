#!/usr/bin/env python3
"""Generate one full CJK LVGL font; no fallback to another pixel size."""
import argparse
import hashlib
import json
from pathlib import Path
import shutil
import subprocess
import tempfile


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--size', type=int, choices=(14, 16, 24), required=True)
    parser.add_argument('--font', type=Path, required=True,
                        help='Pinned NotoSansCJKsc-Regular.otf; see product/assets/fonts/noto-sans-sc')
    parser.add_argument('--converter', default='lv_font_conv',
                        help='lv_font_conv executable')
    parser.add_argument('--symbols-font', type=Path,
                        help='Pinned NotoSansMath-Regular.ttf (default: beside --font)')
    args = parser.parse_args()
    root = Path(__file__).resolve().parents[1]
    provenance = root / 'product/assets/fonts/noto-sans-sc'
    manifest = json.loads((provenance / 'source.json').read_text())
    if hashlib.sha256(args.font.read_bytes()).hexdigest() != manifest['sha256']:
        parser.error('Source font SHA-256 does not match the licensed Noto input')
    symbols = manifest['symbols']
    symbols_path = args.symbols_font or args.font.with_name(symbols['filename'])
    if hashlib.sha256(symbols_path.read_bytes()).hexdigest() != symbols['sha256']:
        parser.error('Symbols font SHA-256 does not match the licensed Noto input')
    executable = shutil.which(args.converter)
    if executable is None:
        parser.error('lv_font_conv executable not found: ' + args.converter)
    converter = str(Path(executable).resolve())
    version = subprocess.run([converter, '--version'], check=True,
                             capture_output=True, text=True).stdout.strip()
    if version != manifest['converter_version']:
        parser.error('Use lv_font_conv ' + manifest['converter_version'])
    destination = root / f'platforms/esp-idf/components/starter_product/src/ui_font_cn_{args.size}.c'
    font_name = f'ui_font_cn_{args.size}'
    if args.size == 14:
        destination = root / 'firmware/esp-idf/esp32p4/waveshare-touch-lcd-43c/main/ui/font/lv_font_cn_14.c'
        font_name = 'lv_font_cn_14'
    command = [converter, '--size', str(args.size), '--bpp', '2',
               '--format', 'lvgl', '--font', manifest['filename'], '--no-kerning',
               '--lv-include', 'lvgl.h', '--lv-font-name', font_name,
               '--symbols', '°℃', '-o', destination.name]
    for interval in ('0x20-0x7e', '0x2000-0x206f', '0x2190-0x22ff',
                     '0x2500-0x25ff', '0x3000-0x3030', '0x3033-0x303f',
                     '0x4e00-0x9fff', '0xff00-0xffef'):
        command.extend(('-r', interval))
    # Vertical two-em repeat marks U+3031/3032 were never in the product font;
    # including them doubles every line's height, even in Latin/Chinese menus.
    command.extend(('--font', symbols['filename'], '--symbols', '≒'))
    # Stable basenames make the generated output independent of checkout paths.
    # Failed conversion must leave the previously checked-in font untouched.
    with tempfile.TemporaryDirectory() as directory:
        temporary = Path(directory)
        (temporary / manifest['filename']).write_bytes(args.font.read_bytes())
        (temporary / symbols['filename']).write_bytes(symbols_path.read_bytes())
        subprocess.run(command, check=True, cwd=temporary)
        generated = (temporary / destination.name).read_text()
        header = ('/* SPDX-License-Identifier: OFL-1.1\n'
                  ' * Noto Sans CJK SC; copyright and license:\n'
                  ' * product/assets/fonts/noto-sans-sc/NOTICE.txt and OFL.txt\n'
                  f" * Source SHA-256: {manifest['sha256']}\n"
                  ' */\n')
        destination.write_text(header + generated.rstrip() + '\n')


if __name__ == '__main__':
    main()
