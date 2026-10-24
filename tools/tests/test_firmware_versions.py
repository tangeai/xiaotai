import re
import os
import subprocess
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
from firmware_version import FirmwareVersion, read_board_version, reserve_esp_build


SEMVER = re.compile(r"[0-9]+\.[0-9]+\.[0-9]+")


class FirmwareVersionTests(unittest.TestCase):
    def test_native_cmake_embeds_counter_and_reconfigures_when_counter_changes(self):
        with tempfile.TemporaryDirectory() as temporary:
            project = Path(temporary)
            counter = project / 'BUILD_NUMBER'
            counter.write_text('7\n')
            helper = REPO_ROOT/'platforms/esp-idf/cmake/xiaotai_build_version.cmake'
            (project/'CMakeLists.txt').write_text(
                'cmake_minimum_required(VERSION 3.16)\nproject(identity NONE)\n'
                'set(PROJECT_VER "1.0.1")\n'
                f'include("{helper}")\n'
                'file(WRITE "${CMAKE_BINARY_DIR}/identity.txt" "${PROJECT_VER}")\n')
            subprocess.run(['cmake', '-G', 'Ninja', '-S', str(project), '-B', str(project/'build')],
                           check=True, capture_output=True)
            self.assertEqual((project/'build/identity.txt').read_text(), '1.0.1+build.7')
            self.assertEqual(counter.read_text(), '7\n')
            # Some CI filesystems round mtimes; make the dependency change
            # unambiguous without waiting for a wall-clock tick.
            os.utime(project/'build/build.ninja', (1, 1))
            counter.write_text('8\n')
            subprocess.run(['cmake', '--build', str(project/'build')],
                           check=True, capture_output=True)
            self.assertEqual((project/'build/identity.txt').read_text(), '1.0.1+build.8')

    def test_concurrent_build_cannot_change_reserved_identity(self):
        with tempfile.TemporaryDirectory() as temporary:
            project = Path(temporary)
            (project/'BUILD_NUMBER').write_text('2\n')
            with reserve_esp_build(project, '1.0.1') as version:
                self.assertEqual(version, '1.0.1+build.3')
                with self.assertRaisesRegex(ValueError, 'another build'):
                    with reserve_esp_build(project, '1.0.1'):
                        self.fail('concurrent build entered')
                self.assertEqual((project/'BUILD_NUMBER').read_text(), '3\n')

    def test_release_version_has_no_automatic_build_suffix(self) -> None:
        self.assertEqual(str(FirmwareVersion.parse("1.2.3")), "1.2.3")
        with self.assertRaises(ValueError):
            FirmwareVersion.parse("1.2.3+build.9")
        with self.assertRaises(ValueError):
            FirmwareVersion.parse("1.2.4-rc.1")

    def test_all_board_firmware_versions_use_user_selected_semver(self) -> None:
        projects = (
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
        def read(*unused):
            events.append("read")
            return FirmwareVersion.parse("1.0.0")
        def compile(*unused, **kwargs):
            events.append("compile")
            return SimpleNamespace(returncode=return_code)
        with mock.patch.object(build, "parse_args", return_value=args), \
             mock.patch.object(build, "load_boards", return_value=[board]), \
             mock.patch.object(build, "find_board", return_value=board), \
             mock.patch.object(build, "print_build", side_effect=command), \
             mock.patch.object(build, "read_board_version", side_effect=read), \
             mock.patch.object(build, "prepare_dependency_lock"), \
             mock.patch.object(build, "normalize_dependency_lock", return_value=False), \
             mock.patch.object(build.subprocess, "run", side_effect=compile), \
             mock.patch("builtins.print"):
            result = build.main()
        return result, events

    def test_every_real_build_reads_version_without_changing_it(self) -> None:
        for release in (False, True):
            with self.subTest(release=release):
                self.assertEqual(self.dispatch(release=release),
                                 (0, ["validate-selection", "read", "compile"]))

    def test_dry_run_does_not_change_version(self) -> None:
        self.assertEqual(self.dispatch(dry_run=True), (0, ["validate-selection"]))

    def test_failed_build_does_not_change_version(self) -> None:
        self.assertEqual(self.dispatch(return_code=1),
                         (1, ["validate-selection", "read", "compile"]))

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

    def test_read_board_version_does_not_edit_project_or_profile(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            project = root / "firmware/esp-idf/esp32s3/lckfb-esp32s3-xiaotai"
            profile = root / "platforms/esp-idf/components/starter_runtime/src"
            project.mkdir(parents=True)
            profile.mkdir(parents=True)
            (project / "CMakeLists.txt").write_text(
                'set(PROJECT_VER "1.0.0")\n', encoding="utf-8"
            )
            profile_file = profile / "starter_runtime.c"
            profile_file.write_text(
                '"board_model":"waveshare-esp32p4-touch-lcd-43c-v10"},'
                '"firmware_version":"1.0.0"\n'
                '"board_model":"lckfb-esp32s3"},'
                '"firmware_version":"1.0.0"\n',
                encoding="utf-8",
            )

            self.assertEqual(
                str(read_board_version(root, "lckfb-esp32s3")),
                "1.0.0",
            )
            updated = profile_file.read_text(encoding="utf-8")
            self.assertIn(
                '"board_model":"waveshare-esp32p4-touch-lcd-43c-v10"},'
                '"firmware_version":"1.0.0"', updated
            )
            self.assertIn(
                '"board_model":"lckfb-esp32s3"},'
                '"firmware_version":"1.0.0"', updated
            )

    def test_dispatch_allocates_build_identity_even_after_failed_build(self):
        with tempfile.TemporaryDirectory() as temporary:
            project = Path(temporary)
            counter = project / "BUILD_NUMBER"
            counter.write_text("0\n")
            (project / "CMakeLists.txt").write_text('set(PROJECT_VER "1.0.1")\n')
            args = SimpleNamespace(list_boards=False, validate=False, all=False,
                board="lckfb-esp32s3", variant=None, release=True,
                dry_run=False, keep_going=False)
            board = SimpleNamespace(id=args.board, data={"platform": "esp-idf"},
                                    default_variant=lambda: "default")
            seen = []
            def compile(*unused, **kwargs):
                seen.append(counter.read_text().strip())
                return SimpleNamespace(returncode=1 if len(seen) == 1 else 0)
            with mock.patch.object(build, "parse_args", return_value=args), \
                 mock.patch.object(build, "load_boards", return_value=[board]), \
                 mock.patch.object(build, "find_board", return_value=board), \
                 mock.patch.object(build, "print_build", return_value=(["idf.py", "build"], project)), \
                 mock.patch.object(build, "read_board_version", return_value=FirmwareVersion.parse("1.0.1")), \
                 mock.patch.object(build, "normalize_dependency_lock", return_value=False), \
                 mock.patch.object(build.subprocess, "run", side_effect=compile):
                self.assertEqual(build.main(), 1)
                self.assertEqual(build.main(), 0)
                args.dry_run = True
                args.release = False
                self.assertEqual(build.main(), 0)
            self.assertEqual(seen, ["1", "2"])
            self.assertEqual(counter.read_text(), "2\n")
            self.assertIn('"1.0.1"', (project / "CMakeLists.txt").read_text())


if __name__ == "__main__":
    unittest.main()
