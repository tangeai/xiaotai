"""Execute component CMake and verify each board links one selected font."""
import json
import re
from pathlib import Path
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]
COMPONENT = ROOT / 'platforms/esp-idf/components/starter_product'


class FontSelection(unittest.TestCase):
    def test_fixed_text_rows_fit_generated_font_and_do_not_overlap(self):
        source = (COMPONENT / 'src/starter_product.c').read_text()
        for size, scale in ((16, 1), (24, 2)):
            font = (COMPONENT / f'src/ui_font_cn_{size}.c').read_text()
            line_height = int(re.search(r'\.line_height = (\d+)', font).group(1))
            for label in ('s_header_title', 's_recognition_label',
                          's_recognition_result', 's_state', 'name'):
                height = int(re.search(r'lv_obj_set_height\(' + label + r', (\d+)\)', source).group(1))
                self.assertGreaterEqual(height * scale, line_height, (size, label))
            status_y = int(re.search(r's_recognition_label = make_label\(screen, "", 16, (\d+)', source).group(1))
            result_y = int(re.search(r'lv_obj_set_pos\(result_viewport, 16, (\d+)\)', source).group(1))
            viewport_h = int(re.search(r'lv_obj_set_size\(result_viewport, 236, (\d+)\)', source).group(1))
            self.assertGreaterEqual((status_y - 150) * scale, line_height)
            self.assertGreaterEqual((result_y - status_y) * scale, line_height)
            self.assertGreaterEqual(viewport_h * scale, line_height)
            self.assertLessEqual(result_y + viewport_h, 222)

    def test_board_layout_selects_exactly_one_font(self):
        for board in ('boards/lckfb/esp32s3/board.json',
                      'boards/waveshare/esp32p4-touch-lcd-43c/board.json',
                      'boards/m5stack/cores3-lite/board.json'):
            manifest = json.loads((ROOT / board).read_text())
            layout = manifest['variants'][0]['layout_profile']
            size = 24 if layout == 'large' else 16
            wrapper = ROOT / manifest['project_dir'] / 'components/starter_product/CMakeLists.txt'
            self.assertIn(f'set(XIAOTAI_STARTER_PRODUCT_LAYOUT_PROFILE {layout})', wrapper.read_text())
            with tempfile.TemporaryDirectory() as directory:
                output = Path(directory) / 'sources'
                script = Path(directory) / 'test.cmake'
                script.write_text(f'''
set(XIAOTAI_STARTER_PRODUCT_ASSET_DIR "{ROOT}/product/assets/audio")
set(XIAOTAI_REPO_ROOT "{ROOT}")
set(XIAOTAI_STARTER_PRODUCT_LAYOUT_PROFILE {layout})
function(idf_component_register)
  cmake_parse_arguments(ARG "" "" "SRCS;INCLUDE_DIRS;PRIV_INCLUDE_DIRS;REQUIRES" ${{ARGN}})
  file(WRITE "{output}" "${{ARG_SRCS}}")
endfunction()
function(target_add_binary_data)
endfunction()
function(target_compile_definitions)
endfunction()
include("{COMPONENT}/CMakeLists.txt")
''')
                subprocess.run(['cmake', '-P', str(script)], check=True, capture_output=True)
                sources = output.read_text().split(';')
                fonts = [Path(s).name for s in sources if '/ui_font_cn_' in s]
                self.assertEqual(fonts, [f'ui_font_cn_{size}.c'])

    def test_no_direct_small_font_or_cross_size_fallback(self):
        source = (COMPONENT / 'src/starter_product.c').read_text()
        self.assertFalse('&ui_font_cn_16' in source)
        self.assertFalse('&ui_font_cn_24' in source)
        font = (COMPONENT / 'src/ui_font_cn_24.c').read_text()
        self.assertFalse('ui_font_cn_16' in font)


if __name__ == '__main__':
    unittest.main()
