"""Reject unverified font inputs before invoking the public font generator."""
import re
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]


class FontProvenance(unittest.TestCase):
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
