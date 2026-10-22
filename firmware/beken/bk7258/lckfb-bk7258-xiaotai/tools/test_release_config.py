import tempfile
import unittest
from pathlib import Path

import sys
sys.path.insert(0, str(Path(__file__).resolve().parents[5] / "platforms/beken/tools"))
from prepare_release_config import prepare_release_config


class ReleaseConfigTest(unittest.TestCase):
    def test_release_disables_development_critical_lr_recording(self):
        with tempfile.TemporaryDirectory() as temporary:
            source = Path(temporary) / 'config'
            output = Path(temporary) / 'release.config'
            contents = 'CONFIG_LOG_LEVEL=4\nCONFIG_CRITICAL_LR_RECORD=y\n'
            source.write_text(contents)
            prepare_release_config(source, output)
            self.assertIn('# CONFIG_CRITICAL_LR_RECORD is not set', output.read_text())
            self.assertEqual(source.read_text(), contents)

    def test_release_uses_info_and_preserves_other_options(self):
        with tempfile.TemporaryDirectory() as temporary:
            source = Path(temporary) / "config"
            output = Path(temporary) / "release.config"
            source.write_text(
                "CONFIG_FOO=y\nCONFIG_LOG_LEVEL=4\nCONFIG_BAR=7\n",
                encoding="utf-8",
            )
            prepare_release_config(source, output)
            self.assertEqual(
                output.read_text(encoding="utf-8"),
                "CONFIG_FOO=y\nCONFIG_LOG_LEVEL=3\nCONFIG_BAR=7\n",
            )

    def test_missing_or_duplicate_log_level_is_rejected(self):
        with tempfile.TemporaryDirectory() as temporary:
            source = Path(temporary) / "config"
            output = Path(temporary) / "release.config"
            source.write_text("CONFIG_FOO=y\n", encoding="utf-8")
            with self.assertRaisesRegex(ValueError, "exactly one"):
                prepare_release_config(source, output)
            source.write_text(
                "CONFIG_LOG_LEVEL=4\nCONFIG_LOG_LEVEL=6\n",
                encoding="utf-8",
            )
            with self.assertRaisesRegex(ValueError, "exactly one"):
                prepare_release_config(source, output)


if __name__ == "__main__":
    unittest.main()
