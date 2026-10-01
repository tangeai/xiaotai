import tempfile
import unittest
from pathlib import Path

from prepare_release_config import prepare_release_config


class ReleaseConfigTest(unittest.TestCase):
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
