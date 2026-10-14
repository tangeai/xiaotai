"""Boot entry points must report the selected image identity early."""

from pathlib import Path
import unittest


ROOT = Path(__file__).resolve().parents[2]
BK = ROOT / 'firmware/beken/bk7258/lckfb-bk7258-xiaotai'
P4 = ROOT / 'firmware/esp-idf/esp32p4/waveshare-touch-lcd-43c'
S3 = ROOT / 'platforms/esp-idf/main/app_main.c'
ATK = ROOT / 'firmware/esp-idf/esp32s3/atk-dnesp32s3/main/app_main.c'


class BootIdentityTest(unittest.TestCase):
    def test_bk_boot_uses_selected_version_in_ap_image(self):
        cmake = (BK / 'ap/CMakeLists.txt').read_text()
        build = (BK / 'tools/build.sh').read_text()
        entry = (BK / 'ap/ap_main.c').read_text()
        self.assertIn('VERSION.md', cmake)
        self.assertIn('XIAOTAI_FIRMWARE_VERSION', cmake)
        self.assertIn('XIAOTAI_BOOT project=lckfb-bk7258-xiaotai version=%s', entry)
        self.assertIn('XIAOTAI_FIRMWARE_VERSION', entry)
        self.assertIn('APP_VERSION="$firmware_version"', build)
        self.assertLess(entry.index('XIAOTAI_BOOT'), entry.index('media_service_init();'))

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
