#!/usr/bin/env python3
"""Release regressions for the P4 audio, UI and call-control contracts."""

import json
from pathlib import Path
import unittest


ROOT = Path(__file__).resolve().parents[5]
BOARD = ROOT / "firmware/esp-idf/esp32p4/waveshare-touch-lcd-43c"
MEDIA = BOARD / "components/starter_media/src/starter_media.c"
MEDIA_TUNING = BOARD / "main/media/media_tuning.h"
TIRTC = ROOT / "platforms/esp-idf/components/starter_tirtc/src/starter_tirtc.c"
TIRTC_H = ROOT / "platforms/esp-idf/components/starter_tirtc/include/starter_tirtc.h"
PRODUCT = ROOT / "platforms/esp-idf/components/starter_product/src/starter_product.c"
RUNTIME = ROOT / "platforms/esp-idf/components/starter_runtime/src/starter_runtime.c"
APP_MAIN = ROOT / "platforms/esp-idf/main/app_main.c"
KCONFIG = BOARD / "main/Kconfig.projbuild"
SDKCONFIG_DEFAULTS = BOARD / "sdkconfig.defaults"


class P4ReleaseContractTest(unittest.TestCase):
    def test_opus_workers_keep_silk_and_celt_stack_headroom(self) -> None:
        media = MEDIA.read_text()
        self.assertIn(
            "#define OPUS_WORKER_TASK_STACK_BYTES (40U * 1024U)", media
        )
        task_setup = media[media.index("xTaskCreateWithCaps(audio_sink_task"):]
        self.assertGreaterEqual(
            task_setup.count("OPUS_WORKER_TASK_STACK_BYTES"), 2
        )

    def test_ai_audio_is_opus_16k_and_calls_remain_alaw_8k(self) -> None:
        contract = json.loads((BOARD / "platform-media-contract.json").read_text())
        self.assertEqual(contract["ai"]["audio_up"]["codec"], "opus")
        self.assertEqual(contract["ai"]["audio_up"]["sample_rate_hz"], 16000)
        self.assertEqual(contract["ai"]["audio_down"]["codec"], "opus")
        source = TIRTC.read_text()
        self.assertIn("starter_tirtc_send_opus", source)
        self.assertIn("TIRTC_AUDIO_OPUS", source)
        self.assertIn("TIRTC_AUDIOSAMPLE_16K16B1C", source)
        self.assertIn("starter_tirtc_send_opus", TIRTC_H.read_text())
        runtime = RUNTIME.read_text()
        start = runtime[runtime.index("static void send_ai_start"):runtime.index("static void send_room_join")]
        self.assertIn('"codec", "opus"', start)
        self.assertIn('"sample_rate", 16000', start)
        media = MEDIA.read_text()
        self.assertIn("esp_opus_enc_process", media)
        self.assertIn("esp_opus_dec_decode", media)

    def test_p4_uplink_has_aec_ns_agc_and_unified_sensitivity(self) -> None:
        media = MEDIA.read_text()
        self.assertIn("starter_aec_process_capture", media)
        self.assertIn("ns_pro_create", media)
        self.assertIn("ns_process", media)
        self.assertIn("esp_agc_open", media)
        self.assertIn("esp_agc_process", media)
        self.assertIn("starter_media_set_microphone_sensitivity", media)
        sensitivity = media[
            media.index("esp_err_t starter_media_set_microphone_sensitivity"):
            media.index("void starter_media_set_wake_allowed")
        ]
        # esp_codec_dev channel mask 1 addresses physical ES7210 MIC2; this is
        # intentionally different from MIC2's compacted DMA slot index (2).
        self.assertIn("ESP_CODEC_DEV_MAKE_CHANNEL_MASK(1)", sensitivity)
        self.assertIn("set_agc_config(s_agc_8k, 30, 1, 1)", media)
        uplink = media[
            media.index("static void audio_uplink_task"):
            media.index("static void audio_capture_task")
        ]
        self.assertIn("int agc_result = esp_agc_process", uplink)
        self.assertIn("if (agc_result != ESP_AGC_SUCCESS)", uplink)
        self.assertIn("memcpy(processed + offset, agc_input", uplink)
        self.assertIn("s_agc_failures", uplink)
        self.assertIn('"uplink level mode=%d', uplink)
        header = (ROOT / "platforms/esp-idf/components/starter_media_common/include/starter_media.h").read_text()
        self.assertIn("microphone_sensitivity", header)
        product = PRODUCT.read_text()
        self.assertIn('"mic_sens"', product)
        self.assertIn("ACTION_MIC_SENSITIVITY_DOWN", product)
        self.assertIn("ACTION_MIC_SENSITIVITY_UP", product)

    def test_settings_volume_and_sensitivity_share_stepper_interaction(self) -> None:
        product = PRODUCT.read_text()
        self.assertIn("static lv_obj_t *make_settings_stepper", product)
        settings = product[
            product.rindex("static void render_settings(lv_obj_t *screen)"):
            product.rindex("static void refresh_diagnostics")
        ]
        self.assertEqual(settings.count("make_settings_stepper("), 2)
        self.assertIn("ACTION_VOLUME_DOWN, ACTION_VOLUME_UP", settings)
        self.assertIn(
            "ACTION_MIC_SENSITIVITY_DOWN, ACTION_MIC_SENSITIVITY_UP",
            settings,
        )

    def test_text_buttons_use_one_full_coverage_font(self) -> None:
        product = PRODUCT.read_text()
        font = product[
            product.index("static const lv_font_t *product_button_font"):
            product.index("static void set_bg")
        ]
        self.assertIn("return &ui_font_cn_16;", font)
        button = product[
            product.rindex("static lv_obj_t *make_button("):
            product.index("static lv_obj_t *make_settings_stepper")
        ]
        self.assertIn("product_button_font()", button)
        contacts = product[
            product.index("static void render_contacts"):
            product.index("static void render_contact_detail")
        ]
        self.assertIn(
            "lv_obj_set_style_text_font(name, product_button_font(), 0)",
            contacts,
        )

    def test_device_call_video_uses_clear_h264_quality_profile(self) -> None:
        tuning = MEDIA_TUNING.read_text()
        self.assertIn("APP_MEDIA_CALL_VIDEO_WIDTH                      640U", tuning)
        self.assertIn("APP_MEDIA_CALL_VIDEO_HEIGHT                     480U", tuning)
        self.assertIn("APP_MEDIA_CALL_VIDEO_FPS                        12U", tuning)
        self.assertIn("APP_MEDIA_CALL_VIDEO_BITRATE_BPS                1200000U", tuning)
        self.assertIn("APP_MEDIA_CALL_VIDEO_MIN_QP                     28U", tuning)
        self.assertIn("APP_MEDIA_CALL_VIDEO_MAX_QP                     44U", tuning)

    def test_p4_video_transport_has_room_for_audio_and_fast_idr_recovery(self) -> None:
        app_main = APP_MAIN.read_text()
        self.assertIn("CONFIG_XIAOTAI_TIRTC_MAX_SEND_BUFFER_BYTES", app_main)
        self.assertNotIn(".max_send_buffer_bytes = 256U * 1024U", app_main)
        self.assertIn(
            "config XIAOTAI_TIRTC_MAX_SEND_BUFFER_BYTES", KCONFIG.read_text()
        )
        self.assertIn(
            "CONFIG_XIAOTAI_TIRTC_MAX_SEND_BUFFER_BYTES=2097152",
            SDKCONFIG_DEFAULTS.read_text(),
        )
        tuning = MEDIA_TUNING.read_text()
        self.assertIn(
            "APP_MEDIA_CALL_H264_GOP_DURATION_MS             2000U", tuning
        )

    def test_binding_caption_expression_and_hangup_are_user_safe(self) -> None:
        product = PRODUCT.read_text()
        self.assertIn("https://xiaotai.chat", product)
        self.assertIn("请用浏览器打开", product)
        subtitle = product[product.index("s_subtitle = make_label"):]
        self.assertIn("lv_obj_set_style_text_font(s_subtitle, &ui_font_cn_16", subtitle)
        history = product[product.index("s_ai_history_label = make_label"):]
        self.assertIn("lv_obj_set_style_text_font(s_ai_history_label, &ui_font_cn_16", history)
        runtime = RUNTIME.read_text()
        self.assertIn("xiaotai_ai_view_apply", runtime)
        hangup = runtime[runtime.index("static void reject_or_hangup_call"):runtime.index("static void begin_ai_session")]
        self.assertIn("hangup_command", hangup)
        self.assertIn("CALL_HANGUP_FLUSH_MS", hangup)
        self.assertLess(hangup.index("starter_tirtc_send_command"), hangup.index("finish_session(0)"))


if __name__ == "__main__":
    unittest.main()
