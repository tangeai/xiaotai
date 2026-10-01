"""The SZPI project must build the selected board hardware implementation."""

from __future__ import annotations

import subprocess
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
PROJECT = ROOT / "firmware/esp-idf/esp32s3/lckfb-esp32s3-xiaotai"
BOARD = ROOT / "boards/lckfb/esp32s3"


class SzpiBoardAdapterTest(unittest.TestCase):
    def test_media_component_selects_board_adapter(self) -> None:
        cmake = (PROJECT / "components/starter_media/CMakeLists.txt").read_text()
        media = (PROJECT / "components/starter_media/src/starter_media.c").read_text()
        adapter = (BOARD / "board_adapter.c").read_text()
        self.assertIn('"${board_dir}/board_adapter.c"', cmake)
        self.assertIn("szpi_board_init()", media)
        self.assertIn("szpi_board_select_lcd(selected)", media)
        self.assertIn("szpi_board_camera_adapter()", media)
        self.assertIn("s_camera_adapter->start", media)
        self.assertIn("s_camera_adapter->acquire", media)
        self.assertIn("s_camera_adapter->release", media)
        self.assertIn("szpi_board_power_amplifier(enabled)", media)
        self.assertIn("pca9557_write_register", adapter)
        self.assertIn("esp_camera_init(&config)", adapter)
        self.assertNotIn("pca9557_write_register", media)
        self.assertNotIn("esp_camera_init(&config)", media)

    def test_i2s_contract_reads_board_config(self) -> None:
        result = subprocess.run(
            ["bash", str(PROJECT / "tools/check_i2s_resource_policy.sh")],
            cwd=PROJECT,
            capture_output=True,
            text=True,
            check=False,
        )
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)

    def test_audio_hardware_is_owned_by_board_adapter(self) -> None:
        media = (PROJECT / "components/starter_media/src/starter_media.c").read_text()
        adapter = (BOARD / "board_adapter.c").read_text()
        board_config = (BOARD / "board_config.h").read_text()
        for operation in (
            "szpi_board_audio_adapter()",
            "s_audio_adapter->start",
            "s_audio_adapter->read_pcm",
            "s_audio_adapter->write_pcm",
            "s_audio_adapter->set_volume",
            "s_audio_adapter->set_muted",
        ):
            self.assertIn(operation, media)
        self.assertIn("xiaotai_board_audio_adapter_t", adapter)
        for hardware_call in (
            "i2s_new_channel(",
            "esp_codec_dev_read(",
            "esp_codec_dev_write(",
            "es7210_codec_new(",
            "es8311_codec_new(",
        ):
            self.assertIn(hardware_call, adapter)
            self.assertNotIn(hardware_call, media)
        self.assertIn("#define AUDIO_HW_SAMPLE_RATE_HZ 16000U", board_config)

    def test_display_and_touch_hardware_are_owned_by_board(self) -> None:
        product = (
            ROOT / "platforms/esp-idf/components/starter_product/src/starter_product.c"
        ).read_text()
        display = (BOARD / "board_display.c").read_text()
        cmake = (PROJECT / "components/starter_product/CMakeLists.txt").read_text()
        self.assertIn('"${board_dir}/board_display.c"', cmake)
        self.assertIn("szpi_board_display_init(&handles)", product)
        self.assertIn("szpi_board_display_set_awake(awake)", product)
        for call in (
            "esp_lcd_new_panel_st7789(",
            "esp_lcd_touch_new_i2c_ft5x06(",
            "szpi_board_i2c_bus()",
            "szpi_board_select_lcd(true)",
        ):
            self.assertIn(call, display)
            self.assertNotIn(call, product)


if __name__ == "__main__":
    unittest.main()
