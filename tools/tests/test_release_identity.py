"""Release names come from verified package identities, never hand-written titles."""
import hashlib
import json
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest
import zipfile

ROOT = Path(__file__).resolve().parents[2]


class ReleaseIdentityTests(unittest.TestCase):
    def plan(self, directory, board, version, *, corrupt=False):
        path = directory / f'{board}-{version}-validation.zip'
        payload = b'firmware'
        manifest = {'board_id': board, 'project_version': version,
                    'verification_status': 'BUILD_VERIFIED_HIL_PENDING',
                    'application': {'path': 'app.bin', 'sha256': hashlib.sha256(payload).hexdigest()},
                    'files': [{'path': 'app.bin', 'size_bytes': len(payload),
                               'sha256': hashlib.sha256(payload).hexdigest()}]}
        with zipfile.ZipFile(path, 'w') as z:
            z.writestr('MANIFEST.json', json.dumps(manifest))
            z.writestr('app.bin', b'corrupt' if corrupt else payload)
        return subprocess.run([sys.executable, str(ROOT/'tools/release_firmware.py'),
                               '--package', str(path)], capture_output=True, text=True)

    def test_all_boards_have_same_title_rule_with_actual_independent_builds(self):
        with tempfile.TemporaryDirectory() as temporary:
            for board, label, tag, number in [
                ('lckfb-bk7258', 'BK7258', 'bk7258', 56),
                ('lckfb-esp32s3', 'ESP32-S3', 'esp32s3', 1),
                ('waveshare-esp32p4-touch-lcd-43c-v10', 'ESP32-P4', 'esp32p4', 2),
            ]:
                result = self.plan(Path(temporary), board, f'1.0.1+build.{number}')
                self.assertEqual(result.returncode, 0, result.stderr)
                plan = json.loads(result.stdout)
                self.assertEqual(plan['title'], f'{label} v1.0.1 · build.{number}')
                self.assertEqual(plan['tag'], f'{tag}-v1.0.1')

    def test_missing_build_identity_and_corrupt_payload_cannot_be_released(self):
        with tempfile.TemporaryDirectory() as temporary:
            for version, corrupt in [('1.0.1', False), ('1.0.1+build.0', False),
                                     ('1.0.1+build.2', True)]:
                result = self.plan(Path(temporary), 'lckfb-esp32s3', version, corrupt=corrupt)
                self.assertNotEqual(result.returncode, 0)
                self.assertIn('release validation failed', result.stderr)


if __name__ == '__main__':
    unittest.main()
