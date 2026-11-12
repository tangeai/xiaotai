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
    def plan(self, directory, board, version, *, corrupt=False, notes=None):
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
        command = [sys.executable, str(ROOT/'tools/release_firmware.py'), '--package', str(path)]
        if notes is not None:
            note_path = directory / 'notes.md'
            note_path.write_text(notes)
            command.extend(['--notes-file', str(note_path)])
        return subprocess.run(command, capture_output=True, text=True)

    def test_release_notes_reject_incomplete_or_unfilled_bilingual_body(self):
        sys.path.insert(0, str(ROOT / 'tools'))
        from release_firmware import validate_release_notes
        template = (ROOT / '.github/release_notes_template.md').read_text()
        with self.assertRaises(ValueError):
            validate_release_notes(template)
        import re
        complete = re.sub(r'<!--.*?-->', '', template, flags=re.S)
        complete = re.sub(r'\{\{.*?\}\}', 'None', complete)
        validate_release_notes(complete)
        with tempfile.TemporaryDirectory() as tmp:
            result = self.plan(Path(tmp), 'lckfb-esp32s3', '1.1.0+build.1', notes=complete)
            self.assertEqual(result.returncode, 0, result.stderr)
            self.assertTrue(json.loads(result.stdout)['notes_validated'])
            bad = self.plan(Path(tmp), 'lckfb-esp32s3', '1.1.0+build.1', notes=template)
            self.assertNotEqual(bad.returncode, 0)
            unchecked = self.plan(Path(tmp), 'lckfb-esp32s3', '1.1.0+build.1')
            self.assertFalse(json.loads(unchecked.stdout)['notes_validated'])
        for broken in (complete.split('## English')[0],
                       complete.replace('### Fixed', '### Repairs'),
                       complete.replace('### 修复\n\n- None', '### 修复'),
                       complete + '\n{{source_commit}}'):
            with self.assertRaises(ValueError):
                validate_release_notes(broken)

    def test_all_boards_have_same_title_rule_with_actual_independent_builds(self):
        with tempfile.TemporaryDirectory() as temporary:
            for board, label, tag, number in [
                ('lckfb-bk7258', '立创·实战派 BK7258', 'bk7258', 56),
                ('mgkfb-bk7258', '觅感 AI 玩具开发板 BK7258', 'mgkfb-bk7258', 11),
                ('lckfb-esp32s3', '立创·实战派 ESP32-S3', 'lckfb-esp32s3', 1),
                ('waveshare-esp32p4-touch-lcd-43c-v10', '微雪 ESP32-P4-WIFI6-Touch-LCD-4.3', 'waveshare-esp32p4-touch-lcd-43c-v10', 2),
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

    def test_m5_release_uses_board_namespace_and_validates_payload(self):
        with tempfile.TemporaryDirectory() as temporary:
            directory = Path(temporary)
            for number in (41, 42):
                result = self.plan(directory, 'm5stack-cores3-lite', f'1.0.1+build.{number}')
                self.assertEqual(result.returncode, 0, result.stderr)
                plan = json.loads(result.stdout)
                self.assertEqual(plan['tag'], 'm5stack-cores3-lite-v1.0.1')
                self.assertEqual(plan['title'], f'M5Stack CoreS3-Lite v1.0.1 · build.{number}')
                self.assertEqual(plan['board_id'], 'm5stack-cores3-lite')
                self.assertEqual(plan['verification_status'], 'BUILD_VERIFIED_HIL_PENDING')
            corrupt = self.plan(directory, 'm5stack-cores3-lite', '1.0.1+build.42', corrupt=True)
            self.assertNotEqual(corrupt.returncode, 0)
            self.assertIn('artifact checksum mismatch', corrupt.stderr)


if __name__ == '__main__':
    unittest.main()
