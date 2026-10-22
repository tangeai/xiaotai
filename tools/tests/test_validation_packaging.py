from pathlib import Path
import sys
import unittest
from unittest import mock
import hashlib
import json
import tempfile
import zipfile


ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "tools"))

from board_registry import Board
from package_validation_firmware import beken_build_root, package_beken, verification_readme_status


class ValidationPackagingTests(unittest.TestCase):
    def test_beken_variant_delivery_selects_default_eyes_and_explicit_headless(self):
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            project = root / 'firmware/mgkfb-bk7258-xiaotai'
            project.mkdir(parents=True)
            (project/'VERSION.md').write_text('`1.0.1`\n')
            board = Board(root/'board.json', {
                'id': 'mgkfb-bk7258', 'project_dir': 'firmware/mgkfb-bk7258-xiaotai',
                'target': 'bk7258', 'vendor': 'MGKFB', 'model': 'BK7258',
                'sdk': {'name': 'bk_avdk_smp', 'version': 'release/v3.1.1.10'},
                'variants': [{'name': 'eyes', 'default': True},
                             {'name': 'headless', 'default': False}],
            })
            for variant, number in [('eyes', 12), ('headless', 13)]:
                build = project/'build/bk7258'/project.name
                image = variant.encode()
                for relative in ('package/app_pack.rbl', 'package/build_summary.txt',
                                 'bk7258/app.elf', 'partitions/bk_package.json',
                                 'partitions/partitions.json', 'package/all-app.bin'):
                    path = build/relative
                    path.parent.mkdir(parents=True, exist_ok=True)
                    path.write_bytes(image)
                (build/'bk7258_ap').mkdir(exist_ok=True)
                (build/'bk7258_ap/app.elf').write_bytes(f'1.0.1+build.{number}'.encode())
                (build/'build_identity.json').write_text(json.dumps({
                    'version': '1.0.1', 'build_id': str(number),
                    'build_version': f'1.0.1+build.{number}', 'build_profile': 'release', 'variant': variant,
                    'all_app_sha256': hashlib.sha256(image).hexdigest(),
                }))
                for requested in (None, variant):
                    output = root/f'{variant}-{requested}'
                    output.mkdir()
                    with mock.patch('package_validation_firmware.git_output', return_value=''):
                        if requested is None and variant != 'eyes':
                            with self.assertRaisesRegex(ValueError, 'variant'):
                                package_beken(root, board, output, 'release', variant=requested)
                            continue
                        archive = package_beken(root, board, output, 'release', variant=requested)
                    with zipfile.ZipFile(archive) as zipped:
                        manifest = json.loads(zipped.read('MANIFEST.json'))
                        self.assertEqual(manifest['variant'], variant)
                        self.assertEqual(zipped.read(manifest['application']['path']), variant.encode())
                with mock.patch('package_validation_firmware.git_output', return_value=''):
                    wrong = 'headless' if variant == 'eyes' else 'eyes'
                    with self.assertRaisesRegex(ValueError, 'variant'):
                        package_beken(root, board, root, 'release', variant=wrong)
            with self.assertRaises(ValueError):
                package_beken(root, board, root, 'release', variant='other')

    def test_beken_delivery_uses_full_version_and_rejects_wrong_profile(self):
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            project = root / 'firmware/demo'
            build_root = project / 'build/bk7258/demo'
            package = build_root / 'package'
            package.mkdir(parents=True)
            version = '1.2.3+build.40'
            image = b'flash image'
            digest = hashlib.sha256(image).hexdigest()
            for relative in ('package/app_pack.rbl', 'package/build_summary.txt',
                             'bk7258/app.elf', 'partitions/bk_package.json',
                             'partitions/partitions.json'):
                path = build_root / relative
                path.parent.mkdir(parents=True, exist_ok=True)
                path.write_bytes(b'fixture')
            (package / 'all-app.bin').write_bytes(image)
            (project / 'VERSION.md').write_text('`1.2.3`\n')
            (build_root / 'bk7258_ap').mkdir()
            (build_root / 'bk7258_ap/app.elf').write_bytes(version.encode())
            (build_root / 'build_identity.json').write_text(json.dumps({
                'version': '1.2.3', 'build_id': '40', 'build_version': version,
                'build_profile': 'development', 'purpose': '8-second heartbeat candidate',
                'all_app_sha256': digest,
            }))
            board = Board(root / 'board.json', {
                'id': 'lckfb-bk7258', 'project_dir': 'firmware/demo',
                'target': 'bk7258', 'vendor': 'LCKFB', 'model': 'BK7258',
                'sdk': {'name': 'bk_avdk_smp', 'version': 'release/v3.1.1.10'},
            })
            output = root / 'output'
            output.mkdir()
            with mock.patch('package_validation_firmware.git_output', return_value=''):
                archive = package_beken(root, board, output)
            name = f'lckfb-bk7258-{version}-all-app.bin'
            self.assertEqual(archive.name, f'lckfb-bk7258-{version}-validation.zip')
            self.assertEqual((output / name).read_bytes(), image)
            self.assertIn(digest, (output / 'README.md').read_text())
            with zipfile.ZipFile(archive) as zipped:
                manifest = json.loads(zipped.read('MANIFEST.json'))
                self.assertEqual(manifest['project_version'], version)
                self.assertEqual(manifest['application']['path'], name)
                self.assertEqual(zipped.read(name), image)
            identity_path = build_root / 'build_identity.json'
            identity = json.loads(identity_path.read_text())
            identity['build_profile'] = 'release'
            identity_path.write_text(json.dumps(identity))
            with self.assertRaisesRegex(ValueError, 'profile'):
                package_beken(root, board, output)

    def test_verification_status_has_matching_human_readable_text(self) -> None:
        self.assertEqual(
            verification_readme_status("BUILD_VERIFIED_HIL_PENDING"),
            "build verified; this exact artifact still requires HIL.",
        )
        self.assertEqual(
            verification_readme_status("HIL_VERIFIED"),
            "build and hardware-in-the-loop verification passed.",
        )
        with self.assertRaisesRegex(ValueError, "unsupported verification status"):
            verification_readme_status("VERIFIED")

    def test_beken_packaging_selects_the_requested_build_profile(self) -> None:
        project = Path("/workspace/firmware/beken/demo")
        self.assertEqual(
            beken_build_root(project, "bk7258", "release"),
            project / "build" / "bk7258" / "demo",
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
