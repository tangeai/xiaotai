"""Compile actual board-selected caption storage and inspect its ELF sections.

Host section attributes model ESP-IDF's external BSS contract, not PSRAM/cache
hardware. The native firmware ELF must also be inspected after an IDF build.
"""
import subprocess
import tempfile
import unittest
from pathlib import Path

from test_s3_product_regressions import ROOT, function

SOURCE = ROOT / "platforms/esp-idf/components/starter_product/src/starter_product.c"
PROJECTS = {
    "m5": "esp32s3/m5stack-cores3-lite",
    "lckfb": "esp32s3/lckfb-esp32s3-xiaotai",
    "p4": "esp32p4/waveshare-touch-lcd-43c",
}


def board_defines(directory, board, variant="ili9342e"):
    wrapper = ROOT / "firmware/esp-idf" / PROJECTS[board] / "components/starter_product/CMakeLists.txt"
    (directory / "stub.c").write_text("int unused;\n")
    (directory / "CMakeLists.txt").write_text('''
cmake_minimum_required(VERSION 3.19)
project(caption_storage C)
macro(idf_component_register)
  set(COMPONENT_LIB caption_storage)
  add_library(${COMPONENT_LIB} STATIC "${CMAKE_CURRENT_SOURCE_DIR}/stub.c")
endmacro()
function(target_add_binary_data)
endfunction()
''' + f'set(XIAOTAI_BOARD_VARIANT {variant})\ninclude("{wrapper}")\n' + '''
get_target_property(defines ${COMPONENT_LIB} COMPILE_DEFINITIONS)
file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/defines.txt" "${defines}")
''')
    subprocess.run(["cmake", "-S", str(directory), "-B", str(directory / "build")],
                   check=True, capture_output=True, text=True)
    return (directory / "build/defines.txt").read_text().split(";")


class CaptionMemory(unittest.TestCase):
    def compile_case(self, directory, board, variant="ili9342e", external_bss=True):
        defines = board_defines(directory, board, variant)
        source = SOURCE.read_text()
        # Keep production preprocessor guards, declarations and their capacities.
        start = source.index("static char s_last_history_subtitle")
        end = source.index("static char s_call_result", start)
        code = r'''
#include <assert.h>
#include <stdio.h>
#include <string.h>
#if CONFIG_SPIRAM_ALLOW_BSS_SEG_EXTERNAL_MEMORY
#define EXT_RAM_BSS_ATTR __attribute__((section(".ext_ram.bss")))
#else
#define EXT_RAM_BSS_ATTR
#endif
''' + source[start:end] + function(source, "append_ai_history") + r'''
int main(void) {
    assert(sizeof(s_ai_history) == 2048 && sizeof(s_ai_display) == 2304);
    assert(s_ai_history[0] == 0 && s_ai_display[0] == 0);
    append_ai_history(NULL); append_ai_history("");
    append_ai_history("hello"); append_ai_history("hello");
    append_ai_history("world");
    assert(strcmp(s_ai_history, "hello\nworld") == 0);
    char long_caption[4096]; memset(long_caption, 'x', sizeof(long_caption)-1);
    long_caption[sizeof(long_caption)-1] = 0;
    append_ai_history(long_caption);
    assert(strlen(s_ai_history) == sizeof(s_ai_history)-1);
    snprintf(s_ai_display, sizeof(s_ai_display), "%s\n%s", s_ai_history, "next");
    assert(strlen(s_ai_display) == 2052);
    s_ai_history[0] = s_last_history_subtitle[0] = 0;
    append_ai_history("new session");
    assert(strcmp(s_ai_history, "new session") == 0);
    assert(s_ai_display[sizeof(s_ai_display)-1] == 0);
}
'''
        (directory / "caption.c").write_text(code)
        result = subprocess.run([
            "cc", "-std=c11", "-O0", "-Wall", "-Werror",
            f"-DCONFIG_SPIRAM_ALLOW_BSS_SEG_EXTERNAL_MEMORY={int(external_bss)}",
            *(f"-D{define}" for define in defines), str(directory / "caption.c"),
            "-o", str(directory / "caption")], capture_output=True, text=True)
        return result

    def test_selected_boards_place_only_m5_text_in_external_bss(self):
        for board, variant in (("m5", "ili9342c"), ("m5", "ili9342e"),
                               ("lckfb", "default"), ("p4", "default")):
            with self.subTest(board=board, variant=variant), tempfile.TemporaryDirectory() as tmp:
                path = Path(tmp)
                result = self.compile_case(path, board, variant)
                self.assertEqual(result.returncode, 0, result.stderr)
                subprocess.run([str(path / "caption")], check=True)
                symbols = subprocess.check_output(["objdump", "-t", str(path / "caption")], text=True)
                for name, size in (("s_ai_history", 2048), ("s_ai_display", 2304)):
                    row = next(line.split() for line in symbols.splitlines() if line.endswith(" " + name))
                    self.assertEqual(int(row[-2], 16), size)
                    self.assertEqual(row[-3], ".ext_ram.bss" if board == "m5" else ".bss")
                row = next(line.split() for line in symbols.splitlines()
                           if line.endswith(" s_last_history_subtitle"))
                self.assertEqual(row[-3], ".bss")

    def test_m5_rejects_disabled_external_bss_instead_of_silent_internal_fallback(self):
        with tempfile.TemporaryDirectory() as tmp:
            result = self.compile_case(Path(tmp), "m5", external_bss=False)
            self.assertNotEqual(result.returncode, 0)
            self.assertIn("requires external BSS", result.stderr)

    def test_other_boards_do_not_require_external_bss_for_text(self):
        for board in ("lckfb", "p4"):
            with self.subTest(board=board), tempfile.TemporaryDirectory() as tmp:
                result = self.compile_case(Path(tmp), board, external_bss=False)
                self.assertEqual(result.returncode, 0, result.stderr)


if __name__ == "__main__":
    unittest.main()
