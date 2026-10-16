"""Boot entry points must report the selected image identity early."""

from pathlib import Path
import json
import re
import subprocess
import sys
import tempfile
import unittest


ROOT = Path(__file__).resolve().parents[2]
BK = ROOT / 'firmware/beken/bk7258/lckfb-bk7258-xiaotai'
P4 = ROOT / 'firmware/esp-idf/esp32p4/waveshare-touch-lcd-43c'
S3 = ROOT / 'platforms/esp-idf/main/app_main.c'
ATK = ROOT / 'firmware/esp-idf/esp32s3/atk-dnesp32s3/main/app_main.c'


class BootIdentityTest(unittest.TestCase):
    def test_bk_incremental_prepare_invalidates_sdk_environment_cache(self):
        with tempfile.TemporaryDirectory() as temporary:
            directory = Path(temporary)
            counter = directory / 'BUILD_NUMBER'
            counter.write_text('44\n')
            caches = [directory / soc / 'CMakeCache.txt'
                      for soc in ('bk7258_ap', 'bk7258')]
            for cache in caches:
                cache.parent.mkdir()
                cache.write_text('APP_VERSION was configured from the old environment\n')
            subprocess.run([sys.executable, str(BK / 'tools/build_identity.py'),
                            'prepare', str(directory), '1.0.0', 'abcdef',
                            str(counter)], check=True)
            for cache in caches:
                self.assertFalse(cache.exists(), 'SDK must reconfigure APP_VERSION')

    def test_bk_finalize_rejects_mixed_sdk_and_product_versions(self):
        with tempfile.TemporaryDirectory() as temporary:
            directory = Path(temporary)
            counter = directory / 'BUILD_NUMBER'
            counter.write_text('43\n')
            tool = BK / 'tools/build_identity.py'
            subprocess.run([sys.executable, str(tool), 'prepare', str(directory),
                            '1.0.0', 'abcdef', str(counter)], check=True)
            artifact = directory / 'all-app.bin'
            artifact.write_bytes(b'old packaged image')
            elf = directory / 'app.elf'
            elf.write_bytes(b'SDK:1.0.0+build.42\0Product:1.0.0+build.44\0')
            result = subprocess.run([sys.executable, str(tool), 'finalize',
                                     str(directory), str(artifact), str(elf)],
                                    capture_output=True, text=True)
            self.assertNotEqual(result.returncode, 0)
            self.assertIn('mixed build versions', result.stderr)
            self.assertFalse((directory / 'lckfb-bk7258-1.0.0+build.44-all-app.bin').exists())

    def test_bk_finalize_rejects_stale_packaged_ap_payload(self):
        with tempfile.TemporaryDirectory() as temporary:
            directory = Path(temporary)
            counter = directory / 'BUILD_NUMBER'
            counter.write_text('43\n')
            tool = BK / 'tools/build_identity.py'
            subprocess.run([sys.executable, str(tool), 'prepare', str(directory),
                            '1.0.0', 'abcdef', str(counter)], check=True)
            elf = directory / 'app.elf'
            elf.write_bytes(b'1.0.0+build.44\0')
            elf.with_suffix('.bin').write_bytes(b'1.0.0+build.44\0')
            partitions = directory / 'partitions'
            partitions.mkdir()
            (partitions / 'bk_package.json').write_text(json.dumps({
                'crc_enable': True, 'section': [
                    {'firmware': 'app1.bin', 'start_addr': '0x0'}]}))
            artifact = directory / 'all-app.bin'
            artifact.write_bytes(b'1.0.0+build.42\0'.ljust(32, b'\xff') + b'\0\0')
            result = subprocess.run([sys.executable, str(tool), 'finalize',
                                     str(directory), str(artifact), str(elf)],
                                    capture_output=True, text=True)
            self.assertNotEqual(result.returncode, 0)
            self.assertIn('packaged AP payload', result.stderr)

    def test_bk_boot_uses_selected_version_in_ap_image(self):
        cmake = (BK / 'ap/CMakeLists.txt').read_text()
        build = (BK / 'tools/build.sh').read_text()
        entry = (BK / 'ap/ap_main.c').read_text()
        self.assertIn('VERSION.md', cmake)
        self.assertIn('XIAOTAI_FIRMWARE_VERSION', cmake)
        self.assertIn('XIAOTAI_BOOT project=lckfb-bk7258-xiaotai version=%s', entry)
        self.assertIn('XIAOTAI_BUILD_VERSION', entry)
        self.assertIn('bk_printf_raw(BK_LOG_NONE, NULL,', entry)
        self.assertIn('APP_VERSION="$build_version"', build)
        self.assertLess(entry.index('AP boot reached application entry'),
                        entry.index('XIAOTAI_BOOT'))
        self.assertLess(entry.index('XIAOTAI_BOOT'), entry.index('media_service_init();'))

    def test_bk_build_identity_matches_boot_and_artifact(self):
        build = (BK / 'tools/build.sh').read_text()
        cmake = (BK / 'ap/CMakeLists.txt').read_text()
        entry = (BK / 'ap/ap_main.c').read_text()
        self.assertIn('build=%s', entry)
        self.assertIn('XIAOTAI_BUILD_ID', entry)
        self.assertIn('XIAOTAI_BUILD_VERSION', entry)
        self.assertIn('xiaotai_build_identity.h', entry)
        self.assertIn('build_identity.py', build)
        self.assertIn('build_root', build)
        self.assertIn('CMAKE_BINARY_DIR', cmake)
        packaging = (ROOT / 'tools/package_validation_firmware.py').read_text()
        self.assertIn('"build_id": identity["build_id"]', packaging)

        with tempfile.TemporaryDirectory() as temporary:
            directory = Path(temporary)
            identity = directory / 'identity'
            counter = directory / 'BUILD_NUMBER'
            counter.write_text('39\n')
            artifact = directory / 'all-app.bin'
            ap_elf = directory / 'app.elf'
            tool = BK / 'tools/build_identity.py'
            subprocess.run([sys.executable, str(tool), 'prepare',
                            str(identity), '1.2.3', 'abcdef0123456789',
                            str(counter)],
                           check=True)
            header = (identity / 'xiaotai_build_identity.h').read_text()
            manifest = json.loads((identity / 'build_identity.json').read_text())
            self.assertEqual(manifest['build_id'], '40')
            self.assertEqual(manifest['build_number'], 40)
            self.assertEqual(counter.read_text(), '40\n')
            self.assertIn('1.2.3+build.40', header)
            self.assertEqual(manifest['version'], '1.2.3')
            ap_elf.write_bytes(b'elf string table ' +
                               manifest['build_version'].encode())
            ap_bin = ap_elf.with_suffix('.bin')
            ap_bin.write_bytes(b'example firmware ' + manifest['build_version'].encode())
            (identity / 'partitions').mkdir()
            (identity / 'partitions/bk_package.json').write_text(json.dumps({
                'crc_enable': True, 'section': [
                    {'firmware': 'app1.bin', 'start_addr': '0x0'}]}))
            payload = ap_bin.read_bytes()
            artifact.write_bytes(b''.join(payload[i:i+32].ljust(32, b'\xff') + b'\0\0'
                                          for i in range(0, len(payload), 32)))
            subprocess.run([sys.executable, str(tool), 'finalize',
                            str(identity), str(artifact), str(ap_elf)], check=True)
            manifest = json.loads((identity / 'build_identity.json').read_text())
            import hashlib
            self.assertEqual(manifest['all_app_sha256'],
                             hashlib.sha256(artifact.read_bytes()).hexdigest())
            named = directory / 'lckfb-bk7258-1.2.3+build.40-all-app.bin'
            self.assertEqual(named.read_bytes(), artifact.read_bytes())
            notes = (directory / 'README.md').read_text()
            self.assertIn('1.2.3+build.40', notes)
            self.assertIn(manifest['all_app_sha256'], notes)
            self.assertIn('development', notes)
            first_id = manifest['build_id']
            subprocess.run([sys.executable, str(tool), 'prepare',
                            str(identity), '1.2.3', 'abcdef0123456789',
                            str(counter)],
                           check=True)
            next_manifest = json.loads((identity / 'build_identity.json').read_text())
            self.assertEqual(first_id, '40')
            self.assertEqual(next_manifest['build_id'], '41')
            self.assertEqual(counter.read_text(), '41\n')
            with self.assertRaises(subprocess.CalledProcessError):
                subprocess.run([sys.executable, str(tool), 'finalize',
                                str(identity), str(artifact), str(ap_elf)],
                               check=True, capture_output=True)

    def test_bk_platform_payload_reports_the_embedded_build_version(self):
        source = (BK / 'ap/src/xiaotai_platform_client.c').read_text()
        declaration = re.search(r'static const char s_device_profile\[\].*?;',
                                source, re.S).group(0)
        with tempfile.TemporaryDirectory() as temp:
            temp = Path(temp)
            counter = temp / 'BUILD_NUMBER'
            counter.write_text('39\n')
            subprocess.run([sys.executable, str(BK / 'tools/build_identity.py'),
                            'prepare', str(temp), '1.2.3', 'abcdef', str(counter)],
                           check=True)
            harness = temp / 'profile.c'
            harness.write_text('#include <stdio.h>\n'
                               '#include "xiaotai_build_identity.h"\n' +
                               declaration + '\nint main(void) { puts(s_device_profile); }\n')
            executable = temp / 'profile'
            subprocess.run(['cc', str(harness), '-o', str(executable)], check=True)
            body = subprocess.check_output([str(executable)], text=True)
            self.assertEqual(json.loads(body)['firmware_version'], '1.2.3+build.40')

    def test_esp_boots_use_embedded_app_descriptor(self):
        for path in (P4 / 'main/app_main.c', S3, ATK):
            with self.subTest(path=path):
                entry = path.read_text()
                self.assertIn('XIAOTAI_BOOT project=%s version=%s', entry)
                self.assertIn('esp_app_get_description()', entry)
                self.assertIn('esp_get_idf_version()', entry)
                self.assertLess(entry.index('XIAOTAI_BOOT'),
                                entry.index('ESP_ERROR_CHECK(app_init())')
                                if path == P4 / 'main/app_main.c' else
                                entry.index('ESP_ERROR_CHECK(starter_media_init())'))
        p4_app = (P4 / 'main/application/app.c').read_text()
        self.assertNotIn('"firmware version: %s project=%s built=%s %s"', p4_app)


if __name__ == '__main__':
    unittest.main()
