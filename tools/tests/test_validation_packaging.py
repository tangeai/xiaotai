from pathlib import Path
import sys
import unittest


ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "tools"))

from package_validation_firmware import beken_build_root


class ValidationPackagingTests(unittest.TestCase):
    def test_beken_packaging_selects_the_requested_build_profile(self) -> None:
        project = Path("/workspace/firmware/beken/demo")
        self.assertEqual(
            beken_build_root(project, "bk7258", "release"),
            project / "build" / "bk7258" / "demo_release",
        )
        self.assertEqual(
            beken_build_root(project, "bk7258", "development"),
            project / "build" / "bk7258" / "demo",
        )
        with self.assertRaisesRegex(ValueError, "unsupported Beken build profile"):
            beken_build_root(project, "bk7258", "debug")

    def test_unified_packager_discovers_registered_projects(self) -> None:
        source = (ROOT / "tools" / "package_validation_firmware.py").read_text()
        self.assertIn("load_boards(root)", source)
        self.assertIn("find_board(boards, args.board)", source)
        self.assertIn("board.project_path(root)", source)
        self.assertNotIn("ESP_PROJECTS", source)
        self.assertNotIn("BEKEN_PROJECT", source)

    def test_beken_delivery_keeps_full_flash_ota_and_symbols_distinct(self) -> None:
        source = (ROOT / "tools" / "package_validation_firmware.py").read_text()
        self.assertIn('application = package_dir / "all-app.bin"', source)
        self.assertIn('ota_image = package_dir / "app_pack.rbl"', source)
        self.assertIn('f"symbols/{target}_ap.elf"', source)
        self.assertIn('f"symbols/{target}_cp.elf"', source)
        self.assertIn('"BUILD_VERIFIED_HIL_PENDING"', source)
        self.assertIn('project_dir / "VERSION.md"', source)
        self.assertNotIn('version = "V1.0.0"', source)


if __name__ == "__main__":
    unittest.main()
