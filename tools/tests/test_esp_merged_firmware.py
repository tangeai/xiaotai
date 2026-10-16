"""Exercise merged delivery from a verified split package, including P4 offsets."""
import hashlib
import json
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest
from unittest.mock import patch
import zipfile

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import merge_esp_firmware as merged
import package_validation_firmware as packaging
from board_registry import Board
from release_firmware import release_plan


class MergedFirmwareTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.directory = Path(self.temp.name)

    def fixture(self, chip='esp32s3', *, overlap=False, corrupt=False):
        board = ('lckfb-esp32s3' if chip == 'esp32s3' else
                 'waveshare-esp32p4-touch-lcd-43c-v10')
        version = '1.0.1+build.2'
        package = self.directory / f'{board}-{version}-validation.zip'
        self.parts = [(0 if chip == 'esp32s3' else 0x2000, 'bootloader/bootloader.bin', b'BOOT'),
                      (0x8000, 'partition_table/partition-table.bin', b'PART'),
                      (0x10000, 'app.bin', b'APPLICATION')]
        if chip == 'esp32p4':
            self.parts.insert(2, (0xd000, 'ota_data_initial.bin', b'OTA'))
        if overlap:
            self.parts[-1] = (0x8001, 'app.bin', b'APPLICATION')
        files = {hex(offset): name for offset, name, _ in self.parts}
        settings = {'flash_mode': 'dio', 'flash_freq': '80m',
                    'flash_size': '16MB' if chip == 'esp32s3' else '32MB'}
        flasher = {'flash_files': files, 'extra_esptool_args': {'chip': chip},
                   'flash_settings': settings,
                   'bootloader': {'offset': hex(self.parts[0][0]), 'file': self.parts[0][1]},
                   'partition-table': {'offset': '0x8000', 'file': self.parts[1][1]},
                   'app': {'offset': hex(self.parts[-1][0]), 'file': 'app.bin'}}
        payloads = {name: data for _, name, data in self.parts}
        payloads['flasher_args.json'] = json.dumps(flasher).encode()
        records = [{'path': name, 'size_bytes': len(data),
                    'sha256': hashlib.sha256(data).hexdigest()} for name, data in payloads.items()]
        manifest = {'board_id': board, 'project_version': version,
                    'verification_status': 'BUILD_VERIFIED_HIL_PENDING',
                    'image_identity': {'target': chip},
                    'application': records[2 if chip == 'esp32s3' else 3],
                    'flash_files': [{'offset': offset, 'file': name} for offset, name in files.items()],
                    'files': records}
        with zipfile.ZipFile(package, 'w') as z:
            for name, data in payloads.items():
                z.writestr(name, b'corrupt' if corrupt and name == 'app.bin' else data)
            z.writestr('MANIFEST.json', json.dumps(manifest))
        return package

    def vendor_merge(self, command, **kwargs):
        # Stand-in for the external esptool process, not the production validator.
        self.assertEqual(command[1:3], ['-m', 'esptool'])
        self.assertIn('merge_bin', command)
        output = Path(command[command.index('--output') + 1])
        data = bytearray(b'\xff' * (self.parts[-1][0] + len(self.parts[-1][2])))
        for offset, name, payload in self.parts:
            self.assertIn(hex(offset), command)
            self.assertEqual(Path(command[command.index(hex(offset)) + 1]).read_bytes(), payload)
            data[offset:offset + len(payload)] = payload
        output.write_bytes(data)
        return subprocess.CompletedProcess(command, 0, '', '')

    def test_s3_and_p4_publish_sibling_without_changing_zip_or_input_bytes(self):
        for chip in ('esp32s3', 'esp32p4'):
            with self.subTest(chip=chip):
                package = self.fixture(chip)
                original = package.read_bytes()
                with patch.object(merged.subprocess, 'run', side_effect=self.vendor_merge):
                    output = merged.merge_package(package)
                self.assertEqual(output.name, package.name.replace('-validation.zip', '-merged.bin'))
                self.assertEqual(package.read_bytes(), original)
                for offset, _, payload in self.parts:
                    self.assertEqual(output.read_bytes()[offset:offset + len(payload)], payload)
                self.assertEqual(output.read_bytes()[0x9000:0xd000], b'\xff' * 0x4000)
                plan = release_plan(package, merged=output)
                self.assertEqual(plan['merged']['flash_offset'], '0x0')
                self.assertEqual(plan['merged']['sha256'], hashlib.sha256(output.read_bytes()).hexdigest())
                self.assertTrue(plan['merged']['overwrites_gaps'])

    def test_corruption_and_overlapping_regions_rejected_before_esptool(self):
        for options in ({'corrupt': True}, {'overlap': True}):
            package = self.fixture(**options)
            with patch.object(merged.subprocess, 'run') as run:
                with self.assertRaises(ValueError):
                    merged.merge_package(package)
                run.assert_not_called()

    def test_bad_merged_bytes_and_padding_cannot_be_released(self):
        package = self.fixture()
        with patch.object(merged.subprocess, 'run', side_effect=self.vendor_merge):
            output = merged.merge_package(package)
        original = output.read_bytes()
        for offset in (0, 0x9000, 0x10000):
            data = bytearray(original)
            data[offset] ^= 1
            output.write_bytes(data)
            with self.assertRaises(ValueError):
                release_plan(package, merged=output)

    def test_tool_failure_does_not_publish_partial_image(self):
        package = self.fixture()
        with patch.object(merged.subprocess, 'run', side_effect=subprocess.CalledProcessError(1, 'esptool')):
            with self.assertRaises(subprocess.CalledProcessError):
                merged.merge_package(package)
        self.assertFalse(package.with_name(package.name.replace('-validation.zip', '-merged.bin')).exists())

    def test_incorrect_vendor_output_never_replaces_existing_image(self):
        package = self.fixture()
        with patch.object(merged.subprocess, 'run', side_effect=self.vendor_merge):
            output = merged.merge_package(package)
        original = output.read_bytes()

        def wrong_output(command, **kwargs):
            self.vendor_merge(command, **kwargs)
            target = Path(command[command.index('--output') + 1])
            target.write_bytes(b'wrong')

        with patch.object(merged.subprocess, 'run', side_effect=wrong_output):
            with self.assertRaisesRegex(ValueError, 'length'):
                merged.merge_package(package)
        self.assertEqual(output.read_bytes(), original)

    def test_unified_packaging_writes_checksums_for_zip_and_merged(self):
        package = self.fixture()
        board = Board(self.directory / 'board.json', {'id': 'lckfb-esp32s3',
                      'project_dir': 'demo', 'platform': 'esp-idf'})
        with patch.object(sys, 'argv', ['package_validation_firmware.py', '--board', board.id,
                                      '--output', str(self.directory)]), \
             patch.object(packaging, 'load_boards', return_value=[board]), \
             patch.object(packaging, 'package_board', return_value=package), \
             patch.object(merged.subprocess, 'run', side_effect=self.vendor_merge):
            self.assertEqual(packaging.main(), 0)
        lines = (self.directory / 'SHA256SUMS').read_text().splitlines()
        self.assertEqual(len(lines), 2)
        for line in lines:
            digest, name = line.split('  ')
            self.assertEqual(digest, hashlib.sha256((self.directory / name).read_bytes()).hexdigest())


if __name__ == '__main__':
    unittest.main()
