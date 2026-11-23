"""Reject unverified font inputs before invoking the public font generator."""
import os
import re
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]


class FontProvenance(unittest.TestCase):
    def test_lvgl_looks_up_valid_glyphs_without_returning_unmapped_characters(self):
        installed = ROOT / ('firmware/esp-idf/esp32s3/lckfb-esp32s3-xiaotai/'
                            'managed_components/lvgl__lvgl')
        lvgl = Path(os.environ.get('XIAOTAI_HOST_LVGL', installed))
        if not (lvgl / 'lvgl.h').is_file():
            if 'XIAOTAI_HOST_LVGL' in os.environ:
                self.fail(f'LVGL checkout missing: {lvgl}')
            self.skipTest('LVGL source checkout unavailable')

        sources = [
            'src/font/lv_font_fmt_txt.c',
            'src/misc/lv_mem.c',
            'src/misc/lv_utils.c',
        ]
        with tempfile.TemporaryDirectory(prefix='xiaotai-font-lookup-') as directory:
            path = Path(directory)
            harness = path / 'lookup.c'
            harness.write_text(r'''
#include <assert.h>
#include "lvgl.h"

extern const lv_font_t FONT;

int main(void)
{
    assert(LVGL_VERSION_MAJOR == 8 && LVGL_VERSION_MINOR == 3 && LVGL_VERSION_PATCH == 11);
    lv_font_glyph_dsc_t glyph;
    const uint32_t present[] = {'A', '~', 0x00b0, 0x2103, 0x2252,
                                0x4e00, 0x4eef, 0x4ef0, 0xff01, 0xffbe, 0xffc2, 0xffee};
    for (unsigned i = 0; i < sizeof(present) / sizeof(present[0]); ++i) {
        assert(FONT.get_glyph_dsc(&FONT, &glyph, present[i], 0));
        assert(glyph.adv_w && glyph.box_h && glyph.box_w);
    }
    const uint32_t absent[] = {0x007f, 0x3031, 0xffbf, 0xffc8};
    for (unsigned i = 0; i < sizeof(absent) / sizeof(absent[0]); ++i) {
        assert(!FONT.get_glyph_dsc(&FONT, &glyph, absent[i], 0));
    }

    unsigned mapped = 0;
    for (uint32_t codepoint = 0x20; codepoint <= 0xffff; ++codepoint)
        mapped += FONT.get_glyph_dsc(&FONT, &glyph, codepoint, 0);
    assert(mapped == 21693);
}
''')
            esp_product = ROOT / 'platforms/esp-idf/components/starter_product/src'
            p4_font = ROOT / ('firmware/esp-idf/esp32p4/waveshare-touch-lcd-43c/'
                              'main/ui/font/lv_font_cn_14.c')
            for font in (esp_product / 'ui_font_cn_16.c', esp_product / 'ui_font_cn_24.c', p4_font):
                with self.subTest(font=font.name):
                    exe = path / 'lookup'
                    subprocess.run(
                        ['cc', f'-DFONT={font.stem}', '-DLV_CONF_SKIP',
                         '-DLV_FONT_FMT_TXT_LARGE=1', '-DLV_MEM_CUSTOM=1',
                         '-ffunction-sections', '-fdata-sections', '-I', str(lvgl),
                         str(harness), str(font), *(str(lvgl / name) for name in sources),
                         '-Wl,--gc-sections', '-o', str(exe)],
                        check=True)
                    subprocess.run([str(exe)], check=True, cwd=path)

    def test_distributed_character_maps_respect_lvgl_lookup_bounds(self):
        paths = list((ROOT / 'platforms/esp-idf/components/starter_product/src').glob('ui_font_cn_*.c'))
        paths.append(ROOT / 'firmware/esp-idf/esp32p4/waveshare-touch-lcd-43c/main/ui/font/lv_font_cn_14.c')
        for path in paths:
            with self.subTest(path=path):
                source = path.read_text()
                lengths = re.findall(r'\.range_length = ([^,]+),', source)
                self.assertTrue(lengths)
                # These builds pin LVGL 8.3.11, whose upper bound is inclusive.
                self.assertTrue(all(re.fullmatch(r'XIAOTAI_CMAP_LENGTH\(\d+\)', length)
                                    for length in lengths))
                # Sparse Unicode lists make holes unambiguous.
                self.assertNotIn('LV_FONT_FMT_TXT_CMAP_FORMAT0_FULL', source)

    def test_unverified_font_is_rejected_before_converter_runs(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory)
            font = path / 'unlicensed.otf'
            font.write_bytes(b'unverified font')
            marker = path / 'called'
            converter = path / 'converter'
            converter.write_text('#!/bin/sh\ntouch "' + str(marker) + '"\nexit 1\n')
            converter.chmod(0o755)
            result = subprocess.run(
                [sys.executable, str(ROOT / 'tools/generate_product_font.py'),
                 '--size', '16', '--font', str(font), '--converter', str(converter)],
                capture_output=True, text=True)
            self.assertNotEqual(result.returncode, 0)
            self.assertFalse(marker.exists(), 'Unverified font reached converter')
            self.assertIn('SHA-256', result.stderr)

    def test_distributed_fonts_identify_licensed_source(self):
        paths = list((ROOT / 'platforms/esp-idf/components/starter_product/src').glob('ui_font_cn_*.c'))
        paths.append(ROOT / 'firmware/esp-idf/esp32p4/waveshare-touch-lcd-43c/main/ui/font/lv_font_cn_14.c')
        for path in paths:
            with self.subTest(path=path):
                header = path.read_text()[:2000]
                self.assertIn('SPDX-License-Identifier: OFL-1.1', header)
                self.assertIn('NotoSansCJKsc-Regular.otf', header)
                self.assertNotIn('Windows/Fonts', header)


if __name__ == '__main__':
    unittest.main()
