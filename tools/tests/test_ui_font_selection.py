"""Execute component CMake and verify each board links one selected font."""
import json
from pathlib import Path
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]
COMPONENT = ROOT / 'platforms/esp-idf/components/starter_product'


class FontSelection(unittest.TestCase):
    def test_board_layout_selects_exactly_one_font(self):
        for board in ('boards/lckfb/esp32s3/board.json',
                      'boards/waveshare/esp32p4-touch-lcd-43c/board.json'):
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
