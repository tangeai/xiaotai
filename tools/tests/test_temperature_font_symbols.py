"""Production caption fonts must render angle and temperature symbols."""
import re
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]


def glyph_metrics(source, character):
    """Resolve the generated LVGL cmap as the font's public lookup does."""
    codepoint = ord(character)
    pattern = (r'\.range_start = (\d+), \.range_length = (\d+), '
               r'\.glyph_id_start = (\d+),\s*\.unicode_list = (\w+)')
    for start, length, glyph, offsets in re.findall(pattern, source):
        offset = codepoint - int(start)
        if not 0 <= offset < int(length):
            continue
        if offsets != 'NULL':
            values = re.search(r'\b' + offsets + r'\[\] = \{(.*?)\};',
                               source, re.S).group(1)
            values = [int(value, 0) for value in re.findall(r'0x[0-9a-fA-F]+|\d+', values)]
            if offset not in values:
                continue
            offset = values.index(offset)
        descriptors = re.search(r'glyph_dsc\[\] = \{(.*?)\n\};', source, re.S).group(1)
        entries = re.findall(r'\{([^{}]+)\}', descriptors)
        return {key: int(value) for key, value in
                re.findall(r'\.(\w+) = (-?\d+)', entries[int(glyph) + offset])}
    return None


class TemperatureFontSymbols(unittest.TestCase):
    def test_esp_caption_and_control_fonts_have_visible_symbols(self):
        for size in (16, 24):
            source = (ROOT / f'platforms/esp-idf/components/starter_product/src/ui_font_cn_{size}.c').read_text()
            for character in '°℃':
                with self.subTest(size=size, character=character):
                    metrics = glyph_metrics(source, character)
                    self.assertIsNotNone(metrics, f'{size}px lacks {character}')
                    self.assertGreater(metrics['adv_w'], 0)
                    self.assertGreater(metrics['box_w'] * metrics['box_h'], 0)

    def test_beken_ai_caption_font_has_visible_symbols(self):
        source = (ROOT / 'firmware/beken/bk7258/lckfb-bk7258-xiaotai/ap/assets/xiaotai_ui_assets.c').read_text()
        for character in '°℃':
            with self.subTest(character=character):
                glyph = re.search(r'\{0x%04xU, \{(.*?)\}\}' % ord(character), source, re.S)
                self.assertIsNotNone(glyph, f'BK caption lacks {character}')
                self.assertTrue(any(int(value, 16) for value in re.findall(r'0x([0-9a-f]+)', glyph.group(1))))


if __name__ == '__main__':
    unittest.main()
