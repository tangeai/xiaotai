"""Production caption fonts must render angle and temperature symbols."""
from functools import lru_cache
import re
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]


@lru_cache(maxsize=2)
def font_glyphs(source):
    """Resolve actual LVGL cmaps once, including dense and sparse ranges."""
    descriptors = re.search(r'glyph_dsc\[\] = \{(.*?)\n\};', source, re.S).group(1)
    entries = [{key: int(value) for key, value in
                re.findall(r'\.(\w+) = (-?\d+)', item)}
               for item in re.findall(r'\{([^{}]+)\}', descriptors)]
    glyphs = {}
    pattern = (r'\.range_start = (\d+), \.range_length = (\d+), '
               r'\.glyph_id_start = (\d+),\s*\.unicode_list = (\w+)')
    for start, length, first, offsets in re.findall(pattern, source):
        if offsets == 'NULL':
            values = range(int(length))
        else:
            data = re.search(r'\b' + offsets + r'\[\] = \{(.*?)\};', source, re.S).group(1)
            values = [int(value, 0) for value in re.findall(r'0x[0-9a-fA-F]+|\d+', data)]
        for index, offset in enumerate(values):
            glyphs[int(start) + offset] = entries[int(first) + index]
    return glyphs


def glyph_metrics(source, character):
    return font_glyphs(source).get(ord(character))


class TemperatureFontSymbols(unittest.TestCase):
    def test_large_font_covers_product_copy_without_small_fallback(self):
        source = (ROOT / 'platforms/esp-idf/components/starter_product/src/ui_font_cn_24.c').read_text()
        copy = (ROOT / 'product/include/xiaotai_ui_copy.h').read_text()
        characters = set('敏度') | {c for c in copy if '\u4e00' <= c <= '\u9fff'}
        for character in sorted(characters):
            with self.subTest(character=character):
                metrics = glyph_metrics(source, character)
                self.assertIsNotNone(metrics, f'24px lacks {character}')
                self.assertEqual(metrics['adv_w'], 24 * 16)
        self.assertFalse('.fallback = &ui_font_cn_16' in source)

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
