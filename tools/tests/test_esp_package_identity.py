"""Reject mislabeled ESP packages through the public packaging interface."""
import hashlib
import json
from pathlib import Path
import struct
import sys
import tempfile
import unittest
from unittest.mock import patch
import zipfile

sys.path.insert(0,str(Path(__file__).resolve().parents[1]))
from package_esp_validation import package_board
import package_validation_firmware as unified
from board_registry import Board


def image(elf,version='1.0.0',chip=9):
    header=bytearray(24); header[0]=0xe9; header[1]=1; header[23]=1
    struct.pack_into('<H',header,12,chip)
    segment=bytearray(256); struct.pack_into('<I',segment,0,0xabcd5432)
    segment[16:16+len(version)]=version.encode()
    segment[48:52]=b'demo'; segment[144:176]=hashlib.sha256(elf).digest()
    data=header+struct.pack('<II',0x3c000020,len(segment))+segment
    checksum=0xef
    for b in segment: checksum ^= b
    data+=bytes(15-len(data)%16)+bytes([checksum])
    return data+hashlib.sha256(data).digest()


class EspPackageIdentity(unittest.TestCase):
    def setUp(self):
        self.temp=tempfile.TemporaryDirectory(); self.addCleanup(self.temp.cleanup)
        self.root=Path(self.temp.name); self.project=self.root/'demo'
        self.build=self.project/'build'; self.build.mkdir(parents=True)
        self.output=self.root/'out'; self.output.mkdir()
        self.elf=b'\x7fELF'+bytes(200)
        (self.project/'CMakeLists.txt').write_text('set(PROJECT_VER "1.0.0")\n')
        (self.build/'demo.elf').write_bytes(self.elf)
        (self.build/'demo.bin').write_bytes(image(self.elf))
        (self.build/'flash_args').write_text('0x10000 demo.bin')
        (self.build/'project_description.json').write_text(json.dumps({
            'project_version':'1.0.0','project_name':'demo','target':'esp32s3','app_elf':'demo.elf'}))
        (self.build/'flasher_args.json').write_text(json.dumps({
            'app':{'file':'demo.bin'},'flash_files':{'0x10000':'demo.bin'},
            'extra_esptool_args':{'chip':'esp32s3'}}))

    def package(self,**kwargs):
        with patch('package_esp_validation.git_output',return_value='packaging-head'):
            return package_board(self.root,self.project,self.output,'demo-board',**kwargs)

    def test_invalid_image_rejected_before_zip_creation(self):
        (self.build/'demo.bin').write_bytes(b'not an application')
        with self.assertRaises(ValueError): self.package()
        self.assertEqual(list(self.output.iterdir()),[])

    def test_version_chip_elf_and_corruption_are_rejected(self):
        candidates=[image(self.elf,version='0.9.0'),image(self.elf,chip=18),
                    image(b'another ELF'),image(self.elf)[:-1],
                    image(self.elf)[:-1]+b'\xff']
        for data in candidates:
            with self.subTest(digest=hashlib.sha256(data).hexdigest()):
                (self.build/'demo.bin').write_bytes(data)
                with self.assertRaises(ValueError): self.package()

    def test_stale_build_version_rejected(self):
        (self.project/'CMakeLists.txt').write_text('set(PROJECT_VER "2.0.0")\n')
        with self.assertRaises(ValueError): self.package()

    def test_build_number_is_checked_against_actual_image(self):
        (self.project/'BUILD_NUMBER').write_text('7\n')
        version = '1.0.0+build.7'
        desc_path = self.build/'project_description.json'
        desc = json.loads(desc_path.read_text())
        desc['project_version'] = version
        desc_path.write_text(json.dumps(desc))
        (self.build/'demo.bin').write_bytes(image(self.elf, version=version))
        archive = self.package()
        self.assertEqual(archive.name, 'demo-board-1.0.0+build.7-validation.zip')
        with zipfile.ZipFile(archive) as z:
            self.assertEqual(json.loads(z.read('MANIFEST.json'))['image_identity']['version'], version)
        (self.project/'BUILD_NUMBER').write_text('8\n')
        with self.assertRaisesRegex(ValueError, 'stale build'):
            self.package()

    def test_packaging_head_is_not_claimed_as_build_commit(self):
        archive=self.package()
        with zipfile.ZipFile(archive) as z:
            manifest=json.loads(z.read('MANIFEST.json'))
        self.assertIsNone(manifest['source_commit'])
        self.assertEqual(manifest['packaging_source_commit'],'packaging-head')

    def test_unified_cli_forwards_verification_status(self):
        board=Board(self.root/'board.json',{'id':'demo-board',
            'project_dir':'demo','platform':'esp-idf'})
        # This fixture isolates application identity/status forwarding. Full flash
        # inputs and merged delivery are exercised by test_esp_merged_firmware.py.
        merged = self.output/'demo-board-1.0.0-merged.bin'
        merged.write_bytes(b'merged fixture')
        with patch.object(sys,'argv',['package_validation_firmware.py',
                '--board','demo-board','--output',str(self.output),
                '--verification-status','HIL_VERIFIED']), \
             patch.object(unified,'load_boards',return_value=[board]), \
             patch.object(unified,'__file__',str(self.root/'tools/package_validation_firmware.py')), \
             patch.object(unified,'merge_package',return_value=merged), \
             patch('package_esp_validation.git_output',return_value='packaging-head'):
            self.assertEqual(unified.main(),0)
        with zipfile.ZipFile(self.output/'demo-board-1.0.0-validation.zip') as z:
            self.assertEqual(json.loads(z.read('MANIFEST.json'))['verification_status'],
                             'HIL_VERIFIED')

    def test_verification_status_preserved_in_manifest_and_readme(self):
        for status,text in [('BUILD_VERIFIED_HIL_PENDING','still requires HIL'),
                            ('HIL_VERIFIED','hardware-in-the-loop verification passed')]:
            with self.subTest(status=status):
                archive=self.package(verification_status=status)
                with zipfile.ZipFile(archive) as z:
                    self.assertEqual(json.loads(z.read('MANIFEST.json'))['verification_status'],status)
                    self.assertIn(text,z.read('README.txt').decode())
        with self.assertRaises(ValueError): self.package(verification_status='VERIFIED')

if __name__=='__main__': unittest.main()
