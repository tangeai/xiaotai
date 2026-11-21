#!/usr/bin/env python3
"""Generate one full CJK LVGL font; no fallback to another pixel size."""
import argparse
import hashlib
import json
from pathlib import Path
import re
import shutil
import subprocess
import tempfile


def make_lvgl_character_maps_compatible(source):
    """Keep missing codepoints missing on the firmware's pinned LVGL."""
    dense_offsets = re.compile(
        r'static const uint8_t (glyph_id_ofs_list_\d+)\[\] = \{\s*([\d,\s]+)\};\n'
    )
    for match in dense_offsets.finditer(source):
        name, values = match.groups()
        entry = re.compile(
            rf'\.unicode_list = NULL, \.glyph_id_ofs_list = {name}, '
            r'\.list_length = \d+, \.type = LV_FONT_FMT_TXT_CMAP_FORMAT0_FULL'
        )
        if not entry.search(source):
            continue

        # FORMAT0_FULL encodes holes as zero, which LVGL resolves to the first
        # glyph in the map. The converter assigns consecutive IDs to its entries.
        offsets = [int(value) for value in re.findall(r'\d+', values)]
        codepoints = [0] + [index for index, offset in enumerate(offsets) if offset]
        if [offsets[index] for index in codepoints] != list(range(len(codepoints))):
            raise ValueError(f'Unexpected glyph order in {name}')
        unicode_list = name.replace('glyph_id_ofs_list_', 'unicode_list_')
        values = ', '.join(hex(codepoint) for codepoint in codepoints)
        source = source.replace(match[0], f'static const uint16_t {unicode_list}[] = {{\n'
                                f'    {values}\n}};\n', 1)
        source = entry.sub(f'.unicode_list = {unicode_list}, .glyph_id_ofs_list = NULL, '
                           f'.list_length = {len(codepoints)}, .type = LV_FONT_FMT_TXT_CMAP_SPARSE_TINY',
                           source, count=1)

    if 'LV_FONT_FMT_TXT_CMAP_FORMAT0_FULL' in source:
        raise ValueError('Unexpected FORMAT0_FULL character map')

    # LVGL 8.3.11 tests the relative codepoint against an inclusive upper
    # bound. Keep the standard count for LVGL versions with exclusive bounds.
    source = re.sub(r'\.range_length = (\d+),', r'.range_length = XIAOTAI_CMAP_LENGTH(\1),', source)
    marker = 'static const lv_font_fmt_txt_cmap_t cmaps[]'
    return source.replace(marker,
        '/* LVGL 8.3.11 treats range_length as an inclusive upper bound. */\n'
        '#if LVGL_VERSION_MAJOR == 8 && LVGL_VERSION_MINOR == 3 && LVGL_VERSION_PATCH == 11\n'
        '#define XIAOTAI_CMAP_LENGTH(count) ((count) - 1)\n'
        '#else\n'
        '#define XIAOTAI_CMAP_LENGTH(count) (count)\n'
        '#endif\n' + marker, 1)


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
        generated = make_lvgl_character_maps_compatible((temporary / destination.name).read_text())
        header = ('/* SPDX-License-Identifier: OFL-1.1\n'
                  ' * Noto Sans CJK SC; copyright and license:\n'
                  ' * product/assets/fonts/noto-sans-sc/NOTICE.txt and OFL.txt\n'
                  f" * Source SHA-256: {manifest['sha256']}\n"
                  ' */\n')
        destination.write_text(header + generated.rstrip() + '\n')


if __name__ == '__main__':
    main()
