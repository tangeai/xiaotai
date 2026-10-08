#!/usr/bin/env python3
"""Release regressions for the P4 audio, UI and call-control contracts."""

import json
from pathlib import Path
import re
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
PLATFORM_CLIENT = ROOT / "platforms/esp-idf/components/platform_client/src/platform_client.c"
WIFI_MANAGER = ROOT / "platforms/esp-idf/components/wifi_manager/src/wifi_manager.c"
AP_PROTOTYPE = ROOT / "docs/product/ap-provisioning-prototype.html"
KCONFIG = BOARD / "main/Kconfig.projbuild"
SDKCONFIG_DEFAULTS = BOARD / "sdkconfig.defaults"
PRODUCT_FONT_24 = (
    ROOT / "platforms/esp-idf/components/starter_product/src/ui_font_cn_24.c"
)


class P4ReleaseContractTest(unittest.TestCase):
    def test_p4_static_product_copy_is_covered_by_one_24px_font(self) -> None:
        product = PRODUCT.read_text()
        font = PRODUCT_FONT_24.read_text()
        opts = font.split("Opts:", 1)[1].split("--no-kerning", 1)[0]
        symbols = opts.split("--symbols", 1)[1]
        string_literals = "".join(
            re.findall(r'"(?:\\.|[^"\\])*"', product)
        )
        required_copy = set(re.findall(r"[\u3400-\u9fff]", string_literals))
        missing = sorted(required_copy.difference(symbols))
        self.assertEqual(
            missing,
            [],
            f"24px P4 product font falls back to 16px for: {''.join(missing)}",
        )

    def test_p4_runtime_ui_copy_is_covered_by_the_24px_font(self) -> None:
        runtime = RUNTIME.read_text()
        font = PRODUCT_FONT_24.read_text()
        opts = font.split("Opts:", 1)[1].split("--no-kerning", 1)[0]
        symbols = opts.split("--symbols", 1)[1]
        ui_arguments = re.findall(
            r"(?:finish_call_session|product_set_call_result|product_set_room)"
            r"\s*\((.*?)\);",
            runtime,
            re.DOTALL,
        )
        # Ignore function definitions; only call sites feed the 24px result and
        # room-status labels. AI captions use the complete 16px CJK font.
        ui_arguments = [args for args in ui_arguments if "{" not in args]
        ui_literals = "".join(
            literal
            for args in ui_arguments
            for literal in re.findall(r'"((?:\\.|[^"\\])*)"', args)
        )
        required_copy = set(re.findall(r"[\u3400-\u9fff]", ui_literals))
        missing = sorted(required_copy.difference(symbols))
        self.assertEqual(
            missing,
            [],
            f"24px P4 runtime UI font falls back to 16px for: {''.join(missing)}",
        )

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
        self.assertIn("downlink audio decode failed", media)
        self.assertIn("downlink audio played", media)
        self.assertIn("downlink audio write failed", media)
        self.assertIn("downlink audio playback blocked", media)

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
        self.assertIn("return product_ui_font();", font)
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
            "lv_obj_set_style_text_font(name, &ui_font_cn_16, 0)",
            contacts,
        )

    def test_function_menu_uses_header_back_and_one_font_scale(self) -> None:
        product = PRODUCT.read_text()
        menu = product[
            product.index("static void render_menu(lv_obj_t *screen)"):
            product.index("static void render_contacts", product.index("static void render_menu"))
        ]
        menu_button = product[
            product.index("static lv_obj_t *make_menu_button"):
            product.index("static void set_button_text")
        ]

        self.assertIn("make_header_back_button(screen, ACTION_HOME)", menu)
        self.assertNotIn('"网络状态"', menu)
        self.assertNotIn('make_button(screen, "返回"', menu)
        self.assertEqual(menu.count("make_menu_button("), 6)
        self.assertIn("product_ui_font()", menu_button)

    def test_secondary_pages_use_header_back_without_title_overlap(self) -> None:
        product = PRODUCT.read_text()
        self.assertNotIn('"XiaoTai P4 UI ready: 480x320', product)
        self.assertIn("display_driver_width()", product)
        self.assertIn("display_driver_height()", product)
        header = product[
            product.index("static lv_obj_t *make_header_back_button"):
            product.index("static void on_screen_event")
        ]
        self.assertIn("s_header_has_back = true", header)
        title = product[
            product.index("static void render_header(lv_obj_t *screen"):
            product.index("static void render_home")
        ]
        self.assertIn("s_header_has_back ? 50 : 14", title)

        for function, next_function, action in (
            ("render_contacts", "render_contact_detail", "ACTION_MENU"),
            ("render_contact_detail", "render_call", "ACTION_CONTACTS_BACK"),
            ("render_emojis", "render_emoji_preview", "ACTION_MENU"),
            ("render_emoji_preview", "render_settings", "ACTION_EMOJIS"),
            ("render_diagnostics", "render_network", "ACTION_MENU"),
        ):
            section = product[
                product.index(f"static void {function}"):
                product.index(f"static void {next_function}")
            ]
            self.assertIn(f"make_header_back_button(screen, {action})", section)
            self.assertNotIn('make_button(screen, "返回"', section)

        self.assertNotIn(" · ", product)

    def test_device_reset_is_confirmed_and_clears_only_user_namespaces(self) -> None:
        product = PRODUCT.read_text()
        settings = product[
            product.rindex("static void render_settings(lv_obj_t *screen)"):
            product.rindex("static void refresh_diagnostics")
        ]
        reset_task = product[
            product.index("static esp_err_t clear_product_preferences"):
            product.index("static void on_action")
        ]

        self.assertIn("make_header_back_button(screen, ACTION_MENU)", settings)
        self.assertIn('"网络信息"', settings)
        self.assertIn('"重置设备"', settings)
        self.assertIn("PAGE_RESET_CONFIRM", product)
        self.assertIn("ACTION_FACTORY_RESET_CONFIRM", product)
        self.assertIn("wifi_manager_forget_credentials()", reset_task)
        self.assertIn("runtime_config_clear_tirtc()", reset_task)
        self.assertIn("nvs_erase_all(nvs)", reset_task)
        self.assertIn("esp_restart()", reset_task)
        self.assertNotIn("nvs_flash_erase", reset_task)

    def test_home_keeps_last_ai_emotion_after_session_returns_idle(self) -> None:
        product = PRODUCT.read_text()
        runtime = RUNTIME.read_text()
        reset = runtime[
            runtime.index("static void product_snapshot_reset"):
            runtime.index("static void product_set_room")
        ]
        refresh = product[
            product.index("const char *emotion = product.emotion;"):
            product.index("/* 无网络时产品页", product.index("const char *emotion = product.emotion;"))
        ]

        self.assertIn("char emotion[16];", reset)
        self.assertIn("memcpy(emotion, s_product_snapshot.emotion", reset)
        self.assertIn("memcpy(s_product_snapshot.emotion, emotion", reset)
        self.assertNotIn('"calm"', reset)
        self.assertIn("if (emotion[0] == '\\0' || s_prefer_local_expression)", refresh)
        self.assertIn("s_prefer_local_expression = false", product)
        self.assertIn("s_prefer_local_expression = true", product)
        self.assertNotIn("if (session_idle) {", refresh)

    def test_device_call_video_uses_clear_h264_quality_profile(self) -> None:
        tuning = MEDIA_TUNING.read_text()
        self.assertIn("APP_MEDIA_CALL_VIDEO_WIDTH                      640U", tuning)
        self.assertIn("APP_MEDIA_CALL_VIDEO_HEIGHT                     480U", tuning)
        self.assertIn("APP_MEDIA_CALL_VIDEO_FPS                        5U", tuning)
        self.assertIn("APP_MEDIA_CALL_VIDEO_BITRATE_BPS                600000U", tuning)
        self.assertIn("APP_MEDIA_CALL_VIDEO_MIN_QP                     30U", tuning)
        self.assertIn("APP_MEDIA_CALL_VIDEO_MAX_QP                     46U", tuning)

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
        self.assertIn("1. 浏览器打开 https://xiaotai.chat", product)
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

    def test_unbound_device_stays_on_binding_page_and_retries(self) -> None:
        app_main = APP_MAIN.read_text()
        binding = app_main[
            app_main.index("if (!credentials_valid)"):
            app_main.index("} else {", app_main.index("if (!credentials_valid)"))
        ]
        self.assertIn("for (;;)", binding)
        self.assertIn("provision_and_save(mac_address, false)", binding)
        self.assertIn("binding incomplete; retrying", binding)

        platform = PLATFORM_CLIENT.read_text()
        self.assertIn("static volatile bool s_binding_required", platform)
        self.assertIn("bool platform_client_binding_required(void)", platform)
        product = PRODUCT.read_text()
        self.assertIn("platform_client_binding_required()", product)
        binding_transition = product[
            product.index("/* Wi-Fi 已连但尚未绑定时"):
            product.index("/* 上面的通话/绑定跳转")
        ]
        self.assertIn("else if (!binding_required", binding_transition)
        self.assertNotIn("else if (!binding_now", binding_transition)

    def test_confirmed_onboarding_copy_is_shared_with_product_requirements(self) -> None:
        product = PRODUCT.read_text()
        requirements = (ROOT / "docs/product/PRODUCT_REQUIREMENTS.md").read_text()
        wifi = product[
            product.index("static void render_network"):
            product.index("static void render_binding")
        ]
        binding = product[
            product.rindex("static void render_binding"):
            product.rindex("static void render_page")
        ]
        for text in (
            "让设备连接家庭 Wi-Fi",
            "1. 连接设备热点",
            "打开手机“设置 > Wi-Fi”",
            "提示“无互联网”是正常现象",
            "2. 配置家庭网络",
            "未自动打开时，请在浏览器访问",
        ):
            self.assertIn(text, wifi)
            self.assertIn(text, requirements)
        for text in (
            "将设备添加到你的小钛账号",
            "1. 浏览器打开 https://xiaotai.chat",
            "注册或登录账号",
            "2. 选择“添加设备”",
            "输入下面的验证码",
            "正在等待绑定，验证码将语音播报 3 次",
        ):
            self.assertIn(text, binding)
            self.assertIn(text, requirements)

    def test_ap_provisioning_has_one_canonical_two_step_flow(self) -> None:
        requirements = (ROOT / "docs/product/PRODUCT_REQUIREMENTS.md").read_text()
        portal = WIFI_MANAGER.read_text()
        prototype = AP_PROTOTYPE.read_text()
        self.assertIn("唯一产品方案", requirements)
        self.assertIn("两步向导", requirements)
        self.assertNotIn('data-variant="B"', prototype)
        self.assertNotIn('data-variant="C"', prototype)
        self.assertNotIn("切换方案", prototype)
        for text in (
            "小钛联网助手",
            "选择家庭 Wi-Fi",
            "请选择设备要连接的 2.4 GHz 网络",
            "重新扫描",
            "手动输入网络名称",
            "输入 Wi-Fi 密码",
            "密码只保存在小钛设备中",
            "连接此 Wi-Fi",
            "配置已保存",
        ):
            self.assertIn(text, requirements)
            self.assertIn(text, portal)
            self.assertIn(text, prototype)

    def test_contacts_use_complete_three_row_pages(self) -> None:
        product = PRODUCT.read_text()
        contacts = product[
            product.index("static void render_contacts"):
            product.index("static void render_contact_detail")
        ]
        actions = product[product.index("typedef enum {", product.index("product_page_t")):
                          product.index("} product_action_t;")]
        self.assertIn("CONTACTS_PER_PAGE 3U", product)
        self.assertIn("ACTION_CONTACTS_PREVIOUS", actions)
        self.assertIn("ACTION_CONTACTS_NEXT", actions)
        self.assertIn("s_contacts_page * CONTACTS_PER_PAGE", contacts)
        self.assertIn('"上一页"', contacts)
        self.assertIn('"下一页"', contacts)
        self.assertIn('"%u/%u"', contacts)
        self.assertIn("LV_LABEL_LONG_DOT", contacts)
        self.assertIn('make_button(screen, "", 16,', contacts)
        self.assertNotIn("lv_obj_create(screen)", contacts)
        self.assertNotIn("下拉同步", contacts)

    def test_manual_ai_start_waits_for_runtime_readiness(self) -> None:
        runtime = RUNTIME.read_text()
        begin = runtime[
            runtime.index("static void begin_ai_session"):
            runtime.index("static void send_ai_start")
        ]
        loop = runtime[
            runtime.index("static void runtime_task"):
            runtime.index("esp_err_t starter_runtime_start")
        ]
        self.assertIn("s_ai_start_pending", runtime)
        self.assertIn("AI_READY_WAIT_TIMEOUT_MS", runtime)
        self.assertIn("AI 服务启动中，请稍候…", runtime)
        self.assertIn("AI 服务暂不可用，请稍后重试", runtime)
        self.assertIn("platform_ready=%d tirtc_ready=%d microphone_muted=%d", begin)
        self.assertIn("retry_pending_ai_start", loop)
        self.assertNotIn("AI start ignored: platform or TiRTC is not ready", runtime)


if __name__ == "__main__":
    unittest.main()
