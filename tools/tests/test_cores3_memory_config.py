"""Exercise the CoreS3 native configure gate with fresh and retained settings.

Hardware oracle: CoreS3 schematic v1.0 page 3, U7 has only SIO0..SIO3.
This catches the Octal configuration that aborted before app_main on hardware;
it does not emulate a PSRAM chip or certify a successful boot.
"""
from pathlib import Path
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]
PROJECT = ROOT / "firmware/esp-idf/esp32s3/m5stack-cores3-lite"
GATE = PROJECT / "cmake/check_memory_config.cmake"


def config_values(path):
    return dict(line.split("=", 1) for line in path.read_text().splitlines()
                if line.startswith("CONFIG_") and "=" in line)


class CoreS3MemoryConfigTest(unittest.TestCase):
    def configure(self, values):
        with tempfile.TemporaryDirectory() as directory:
            script = Path(directory) / "configure.cmake"
            script.write_text(
                "\n".join(f'set({key} {value})' for key, value in values.items())
                + f'\ninclude("{GATE.as_posix()}")\n')
            return subprocess.run(["cmake", "-P", str(script)],
                                  capture_output=True, text=True)

    def test_fresh_defaults_match_four_data_wire_psram(self):
        values = config_values(PROJECT / "sdkconfig.defaults")
        self.assertEqual(values.get("CONFIG_SPIRAM_MODE_QUAD"), "y",
                         "U7 is Quad PSRAM; Octal aborts before app_main")
        self.assertNotEqual(values.get("CONFIG_SPIRAM_MODE_OCT"), "y")
        result = self.configure(values)
        self.assertEqual(result.returncode, 0, result.stderr)

    def test_retained_octal_sdkconfig_is_rejected(self):
        result = self.configure({"CONFIG_SPIRAM": "y",
                                 "CONFIG_SPIRAM_MODE_OCT": "y"})
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("CoreS3-Lite requires Quad PSRAM", result.stderr)

    def test_quad_configuration_is_accepted(self):
        result = self.configure({"CONFIG_SPIRAM": "y",
                                 "CONFIG_SPIRAM_MODE_QUAD": "y"})
        self.assertEqual(result.returncode, 0, result.stderr)

    def test_disabled_or_conflicting_psram_is_rejected(self):
        for values in ({}, {"CONFIG_SPIRAM_MODE_QUAD": "y"},
                       {"CONFIG_SPIRAM": "y", "CONFIG_SPIRAM_MODE_QUAD": "y",
                        "CONFIG_SPIRAM_MODE_OCT": "y"}):
            with self.subTest(values=values):
                result = self.configure(values)
                self.assertNotEqual(result.returncode, 0)
                self.assertIn("CoreS3-Lite requires Quad PSRAM", result.stderr)

    def test_native_configure_checks_effective_not_only_default_settings(self):
        source = (PROJECT / "CMakeLists.txt").read_text()
        self.assertGreater(source.index('include("${CMAKE_CURRENT_LIST_DIR}/cmake/check_memory_config.cmake")'),
                           source.index("project(m5stack_cores3_lite_xiaotai)"))


if __name__ == "__main__":
    unittest.main()
