import re
import sys
import tempfile
import unittest
from types import SimpleNamespace
from unittest import mock
from pathlib import Path


REPO_ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(REPO_ROOT / "tools"))

from build import build_environment
import build
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

    def dispatch(self, *, release=False, dry_run=False, return_code=0):
        args = SimpleNamespace(list_boards=False, validate=False, all=False,
                               board="lckfb-esp32s3", variant=None,
                               release=release, dry_run=dry_run, keep_going=False)
        board = SimpleNamespace(id=args.board, data={"platform": "esp-idf"},
                                default_variant=lambda: "default")
        events = []
        def command(*unused):
            events.append("validate-selection")
            return ["idf.py", "build"], REPO_ROOT
        def bump(*unused):
            events.append("bump")
            return FirmwareVersion.parse("1.0.0+build.2")
        def compile(*unused, **kwargs):
            events.append("compile")
            return SimpleNamespace(returncode=return_code)
        with mock.patch.object(build, "parse_args", return_value=args), \
             mock.patch.object(build, "load_boards", return_value=[board]), \
             mock.patch.object(build, "find_board", return_value=board), \
             mock.patch.object(build, "print_build", side_effect=command), \
             mock.patch.object(build, "bump_board_build", side_effect=bump), \
             mock.patch.object(build, "prepare_dependency_lock"), \
             mock.patch.object(build, "normalize_dependency_lock", return_value=False), \
             mock.patch.object(build.subprocess, "run", side_effect=compile), \
             mock.patch("builtins.print"):
            result = build.main()
        return result, events

    def test_every_real_build_bumps_once_before_compiling(self) -> None:
        for release in (False, True):
            with self.subTest(release=release):
                self.assertEqual(self.dispatch(release=release),
                                 (0, ["validate-selection", "bump", "compile"]))

    def test_dry_run_does_not_change_version(self) -> None:
        self.assertEqual(self.dispatch(dry_run=True), (0, ["validate-selection"]))

    def test_failed_build_keeps_its_unique_attempt_number(self) -> None:
        self.assertEqual(self.dispatch(return_code=1),
                         (1, ["validate-selection", "bump", "compile"]))

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
