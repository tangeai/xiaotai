import re
import sys
import tempfile
import unittest
from pathlib import Path


REPO_ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(REPO_ROOT / "tools"))

from build import build_environment
from firmware_version import FirmwareVersion, bump_board_build


SEMVER = re.compile(r"[0-9]+\.[0-9]+\.[0-9]+\+build\.[1-9][0-9]*")


class FirmwareVersionTests(unittest.TestCase):
    def test_build_metadata_increments_without_changing_product_version(self) -> None:
        version = FirmwareVersion.parse("1.2.3+build.9")
        self.assertEqual(str(version.bump_build()), "1.2.3+build.10")
        with self.assertRaises(ValueError):
            FirmwareVersion.parse("1.2.3")
        with self.assertRaises(ValueError):
            FirmwareVersion.parse("1.2.4-rc.1")

    def test_all_board_firmware_versions_use_semver_build_metadata(self) -> None:
        projects = (
            "firmware/esp-idf/esp32s3/atk-dnesp32s3/CMakeLists.txt",
            "firmware/esp-idf/esp32s3/lckfb-esp32s3-xiaotai/CMakeLists.txt",
            "firmware/esp-idf/esp32p4/waveshare-touch-lcd-43c/CMakeLists.txt",
        )
        for relative in projects:
            text = (REPO_ROOT / relative).read_text()
            match = re.search(r'set\(PROJECT_VER\s+"([^"]+)"\)', text)
            self.assertIsNotNone(match, relative)
            self.assertRegex(match.group(1), rf"^{SEMVER.pattern}$", relative)

        bk_version = (REPO_ROOT / "firmware/beken/bk7258/"
                      "lckfb-bk7258-xiaotai/VERSION.md").read_text()
        match = re.search(r"`([^`]+)`", bk_version)
        self.assertIsNotNone(match)
        self.assertRegex(match.group(1), rf"^{SEMVER.pattern}$")

    def test_unified_build_only_bumps_explicit_release_builds(self) -> None:
        source = (REPO_ROOT / "tools/build.py").read_text()
        self.assertIn('"--release"', source)
        self.assertIn("bump_board_build", source)
        self.assertIn("if args.release", source)

    def test_release_build_exports_release_profile(self) -> None:
        self.assertEqual(
            build_environment(True, {"KEEP": "yes"}),
            {"KEEP": "yes", "XIAOTAI_BUILD_PROFILE": "release"},
        )
        self.assertEqual(
            build_environment(False, {
                "KEEP": "yes", "XIAOTAI_BUILD_PROFILE": "release"
            }),
            {"KEEP": "yes"},
        )

    def test_bump_updates_only_the_selected_shared_profile(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            project = root / "firmware/esp-idf/esp32s3/lckfb-esp32s3-xiaotai"
            profile = root / "platforms/esp-idf/components/starter_runtime/src"
            project.mkdir(parents=True)
            profile.mkdir(parents=True)
            (project / "CMakeLists.txt").write_text(
                'set(PROJECT_VER "1.0.0+build.1")\n', encoding="utf-8"
            )
            profile_file = profile / "starter_runtime.c"
            profile_file.write_text(
                '"board_model":"waveshare-esp32p4-touch-lcd-43c-v10"},'
                '"firmware_version":"1.0.0+build.1"\n'
                '"board_model":"lckfb-esp32s3"},'
                '"firmware_version":"1.0.0+build.1"\n',
                encoding="utf-8",
            )

            self.assertEqual(
                str(bump_board_build(root, "lckfb-esp32s3")),
                "1.0.0+build.2",
            )
            updated = profile_file.read_text(encoding="utf-8")
            self.assertIn(
                '"board_model":"waveshare-esp32p4-touch-lcd-43c-v10"},'
                '"firmware_version":"1.0.0+build.1"', updated
            )
            self.assertIn(
                '"board_model":"lckfb-esp32s3"},'
                '"firmware_version":"1.0.0+build.2"', updated
            )


if __name__ == "__main__":
    unittest.main()
