"""ESP-IDF products must consume reusable platform components canonically."""

from __future__ import annotations

import sys
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "tools"))

from board_registry import find_board, load_boards  # noqa: E402


BOARDS = load_boards(ROOT)


def project_dir(board_id: str) -> str:
    return find_board(BOARDS, board_id).project_dir


def project_path(board_id: str) -> Path:
    return find_board(BOARDS, board_id).project_path(ROOT)


ATK_PROJECT_DIR = project_dir("alientek-atk-dnesp32s3")
SZPI_PROJECT = project_path("lckfb-esp32s3")
PROJECTS = tuple(
    project_dir(board_id)
    for board_id in (
        "alientek-atk-dnesp32s3",
        "lckfb-esp32s3",
        "waveshare-esp32p4-touch-lcd-43c-v10",
    )
)
RICH_PLATFORM_CLIENT_PROJECTS = tuple(
    project_dir(board_id)
    for board_id in (
        "lckfb-esp32s3",
        "waveshare-esp32p4-touch-lcd-43c-v10",
    )
)


class EspPlatformComponentsTest(unittest.TestCase):
    def test_runtime_config_has_one_platform_implementation(self) -> None:
        canonical = ROOT / "platforms/esp-idf/components/runtime_config"
        self.assertTrue((canonical / "src/runtime_config.c").is_file())
        self.assertTrue((canonical / "include/runtime_config.h").is_file())

        for project_name in PROJECTS:
            component = ROOT / project_name / "components/runtime_config"
            self.assertIn(
                "platforms/esp-idf/components/runtime_config/CMakeLists.txt",
                (component / "CMakeLists.txt").read_text(),
            )
            self.assertFalse((component / "src/runtime_config.c").exists())
            self.assertFalse((component / "include/runtime_config.h").exists())

    def test_platform_component_does_not_depend_on_a_board_project(self) -> None:
        component = ROOT / "platforms/esp-idf/components/runtime_config"
        contents = "\n".join(
            path.read_text() for path in component.rglob("*") if path.is_file()
        )
        for project_name in PROJECTS:
            self.assertNotIn(project_name, contents)

    def test_full_platform_client_is_not_owned_by_szpi_project(self) -> None:
        canonical = ROOT / "platforms/esp-idf/components/platform_client"
        self.assertTrue((canonical / "src/platform_client.c").is_file())
        self.assertTrue((canonical / "include/platform_client.h").is_file())
        for project_name in RICH_PLATFORM_CLIENT_PROJECTS:
            component = ROOT / project_name / "components/platform_client"
            self.assertIn(
                "platforms/esp-idf/components/platform_client/CMakeLists.txt",
                (component / "CMakeLists.txt").read_text(),
            )
            self.assertFalse((component / "src/platform_client.c").exists())
            self.assertFalse((component / "include/platform_client.h").exists())

        policy_tools = SZPI_PROJECT / "tools"
        for tool_name in (
            "check_audio_capture_policy.sh",
            "check_binding_prompt_policy.sh",
            "check_memory_placement_policy.sh",
            "check_realtime_connect_memory_policy.sh",
            "check_time_sync_policy.sh",
            "test_voice_reliability.py",
        ):
            tool = (policy_tools / tool_name).read_text()
            self.assertIn(
                "platforms/esp-idf/components/platform_client/src/platform_client.c",
                tool,
            )

        # ATK still has a deliberately smaller API and remains a separate variant.
        atk = ROOT / ATK_PROJECT_DIR / "components/platform_client"
        self.assertTrue((atk / "src/platform_client.c").is_file())
        self.assertTrue((atk / "include/platform_client.h").is_file())

    def test_button_policy_uses_board_adapters(self) -> None:
        contract = (
            ROOT / "platforms/common/include/xiaotai_board_button.h"
        ).read_text()
        policy = (
            ROOT / "platforms/esp-idf/components/starter_button/src/starter_button.c"
        ).read_text()
        self.assertIn("xiaotai_board_button_adapter_t", contract)
        self.assertIn("xiaotai_board_button_adapter()", policy)
        self.assertNotIn("GPIO_NUM_", policy)

        szpi = (ROOT / "boards/lckfb/esp32s3/board_button.c").read_text()
        self.assertIn("BOARD_AI_BUTTON_GPIO", szpi)
        for board_name in (
            "esp32p4-touch-lcd-43c",
        ):
            config = (
                ROOT / f"boards/waveshare/{board_name}/board_config.h"
            ).read_text()
            self.assertIn("HARDWARE_BOARD_AI_BUTTON_GPIO", config)

        for project_name in RICH_PLATFORM_CLIENT_PROJECTS:
            cmake = (
                ROOT / project_name / "components/starter_button/CMakeLists.txt"
            ).read_text()
            self.assertIn("platforms/esp-idf/components/starter_button", cmake)
            self.assertNotIn("lckfb-szpi-esp32s3-tirtc", cmake)

    def test_full_tirtc_adapter_is_not_owned_by_szpi_project(self) -> None:
        canonical = ROOT / "platforms/esp-idf/components/starter_tirtc"
        self.assertTrue((canonical / "src/starter_tirtc.c").is_file())
        self.assertTrue((canonical / "include/starter_tirtc.h").is_file())

        for project_name in RICH_PLATFORM_CLIENT_PROJECTS:
            component = ROOT / project_name / "components/starter_tirtc"
            self.assertIn(
                "platforms/esp-idf/components/starter_tirtc/CMakeLists.txt",
                (component / "CMakeLists.txt").read_text(),
            )
            self.assertFalse((component / "src/starter_tirtc.c").exists())
            self.assertFalse((component / "include/starter_tirtc.h").exists())

        # ATK uses a smaller SDK/lifecycle surface and remains an explicit variant.
        atk = ROOT / ATK_PROJECT_DIR / "components/starter_tirtc"
        self.assertTrue((atk / "src/starter_tirtc.c").is_file())
        self.assertTrue((atk / "include/starter_tirtc.h").is_file())

    def test_full_console_is_not_owned_by_szpi_project(self) -> None:
        canonical = ROOT / "platforms/esp-idf/components/starter_console"
        self.assertTrue((canonical / "src/starter_console.c").is_file())
        self.assertTrue((canonical / "include/starter_console.h").is_file())

        for project_name in RICH_PLATFORM_CLIENT_PROJECTS:
            component = ROOT / project_name / "components/starter_console"
            self.assertIn(
                "platforms/esp-idf/components/starter_console/CMakeLists.txt",
                (component / "CMakeLists.txt").read_text(),
            )
            self.assertFalse((component / "src/starter_console.c").exists())
            self.assertFalse((component / "include/starter_console.h").exists())

        atk = ROOT / ATK_PROJECT_DIR / "components/starter_console"
        self.assertTrue((atk / "src/starter_console.c").is_file())
        self.assertTrue((atk / "include/starter_console.h").is_file())

    def test_full_runtime_is_not_owned_by_szpi_project(self) -> None:
        canonical = ROOT / "platforms/esp-idf/components/starter_runtime"
        self.assertTrue((canonical / "src/starter_runtime.c").is_file())
        self.assertTrue((canonical / "include/starter_runtime.h").is_file())

        for project_name in RICH_PLATFORM_CLIENT_PROJECTS:
            component = ROOT / project_name / "components/starter_runtime"
            self.assertIn(
                "platforms/esp-idf/components/starter_runtime/CMakeLists.txt",
                (component / "CMakeLists.txt").read_text(),
            )
            self.assertFalse((component / "src/starter_runtime.c").exists())
            self.assertFalse((component / "include/starter_runtime.h").exists())

        atk = ROOT / ATK_PROJECT_DIR / "components/starter_runtime"
        self.assertTrue((atk / "src/starter_runtime.c").is_file())
        self.assertTrue((atk / "include/starter_runtime.h").is_file())

    def test_esp_runtimes_use_the_public_product_session_state_machine(self) -> None:
        public_source = "product/src/xiaotai_runtime.c"
        runtime_components = (
            ROOT / "platforms/esp-idf/components/starter_runtime",
            ROOT / ATK_PROJECT_DIR / "components/starter_runtime",
        )
        for component in runtime_components:
            cmake = (component / "CMakeLists.txt").read_text()
            header = (component / "include/starter_runtime.h").read_text()
            source = (component / "src/starter_runtime.c").read_text()
            self.assertIn(public_source, cmake)
            self.assertIn('#include "xiaotai_runtime.h"', header)
            self.assertIn("xiaotai_runtime_t s_session", source)
            self.assertNotIn("s_session_generation", source)
            self.assertNotIn("s_call_pending_deadline_ms", source)
            business_logic = source.split(
                "starter_runtime_status_t starter_runtime_status", 1
            )[0]
            self.assertNotRegex(
                business_logic,
                r"atomic_load(?:_explicit)?\([^;]*s_public_state",
            )

    def test_full_wifi_manager_is_not_owned_by_szpi_project(self) -> None:
        canonical = ROOT / "platforms/esp-idf/components/wifi_manager"
        for relative in (
            "src/wifi_manager.c",
            "src/captive_dns.c",
            "src/captive_dns.h",
            "include/wifi_manager.h",
        ):
            self.assertTrue((canonical / relative).is_file())

        for project_name in RICH_PLATFORM_CLIENT_PROJECTS:
            component = ROOT / project_name / "components/wifi_manager"
            self.assertIn(
                "platforms/esp-idf/components/wifi_manager/CMakeLists.txt",
                (component / "CMakeLists.txt").read_text(),
            )
            self.assertFalse((component / "src/wifi_manager.c").exists())
            self.assertFalse((component / "include/wifi_manager.h").exists())

        for project_name in RICH_PLATFORM_CLIENT_PROJECTS[1:]:
            port = (ROOT / project_name / "port/CMakeLists.txt").read_text()
            self.assertIn("XIAOTAI_PLATFORM_COMPONENTS", port)
            self.assertNotIn(
                "${XIAOTAI_SOURCE_ROOT}/components/wifi_manager", port
            )

        # ATK's captive portal naming and lifecycle remain a separate variant.
        atk = ROOT / ATK_PROJECT_DIR / "components/wifi_manager"
        self.assertTrue((atk / "src/wifi_manager.c").is_file())
        self.assertTrue((atk / "src/wifi_captive_dns.c").is_file())

    def test_product_composition_root_is_canonical(self) -> None:
        canonical = ROOT / "platforms/esp-idf/main"
        self.assertTrue((canonical / "app_main.c").is_file())
        self.assertTrue((canonical / "Kconfig.projbuild").is_file())

        szpi_cmake = (SZPI_PROJECT / "main/CMakeLists.txt").read_text()
        self.assertIn("platforms/esp-idf/main/CMakeLists.txt", szpi_cmake)
        self.assertFalse(
            (SZPI_PROJECT / "main/app_main.c").exists()
        )

        for project_name in RICH_PLATFORM_CLIENT_PROJECTS[1:]:
            cmake = (ROOT / project_name / "main/CMakeLists.txt").read_text()
            self.assertIn("XIAOTAI_MAIN_EXTRA_REQUIRES p4_hardware", cmake)
            self.assertIn("platforms/esp-idf/main/CMakeLists.txt", cmake)
            voice_kconfig = (
                ROOT / project_name / "components/starter_voice/Kconfig"
            ).read_text()
            self.assertIn("platforms/esp-idf/main/Kconfig.projbuild", voice_kconfig)
            self.assertNotIn("lckfb-szpi-esp32s3-tirtc", voice_kconfig)

    def test_shared_media_algorithms_are_not_owned_by_a_board_project(self) -> None:
        canonical = ROOT / "platforms/esp-idf/components/starter_media_common"
        for relative in (
            "include/starter_media.h",
            "include/starter_aec.h",
            "include/starter_audio_resampler.h",
            "include/starter_preroll.h",
            "include/starter_signal_level.h",
            "src/starter_aec.c",
            "src/starter_audio_resampler.c",
            "src/starter_preroll.c",
        ):
            self.assertTrue((canonical / relative).is_file())

        for project_name in RICH_PLATFORM_CLIENT_PROJECTS:
            component = ROOT / project_name / "components/starter_media"
            cmake = (component / "CMakeLists.txt").read_text()
            self.assertIn("starter_media_common/CMakeLists.txt", cmake)
            self.assertTrue((component / "src/starter_media.c").is_file())
            self.assertFalse((component / "src/starter_aec.c").exists())
            self.assertFalse((component / "src/starter_preroll.c").exists())
            self.assertNotIn("lckfb-szpi-esp32s3-tirtc/components/starter_media", cmake)

        # Hardware transport remains board-owned; only normalized algorithms move.
        self.assertTrue(
            (SZPI_PROJECT / "components/starter_media/src/starter_media.c").is_file()
        )

    def test_voice_engine_is_canonical_with_target_owned_models(self) -> None:
        canonical = ROOT / "platforms/esp-idf/components/starter_voice"
        for relative in (
            "include/starter_voice.h",
            "src/starter_voice.c",
            "src/wake_backend.cpp",
            "src/wake_window.h",
            "vendor/mel_extractor.c",
            "vendor/mel_filterbank.h",
        ):
            self.assertTrue((canonical / relative).is_file())

        for project_name in RICH_PLATFORM_CLIENT_PROJECTS:
            component = ROOT / project_name / "components/starter_voice"
            cmake = (component / "CMakeLists.txt").read_text()
            self.assertIn("XIAOTAI_STARTER_VOICE_MODEL_DIR", cmake)
            self.assertIn("platforms/esp-idf/components/starter_voice", cmake)
            self.assertTrue((component / "model/head.h").is_file())
            self.assertTrue((component / "model/nihaoxiaotai.tflite").is_file())
            self.assertFalse((component / "src/starter_voice.c").exists())
            self.assertFalse((component / "include/starter_voice.h").exists())
            self.assertNotIn("lckfb-szpi-esp32s3-tirtc", cmake)

    def test_product_ui_is_canonical_with_board_injected_display(self) -> None:
        canonical = ROOT / "platforms/esp-idf/components/starter_product"
        for relative in (
            "include/starter_product.h",
            "src/starter_product.c",
            "src/ui_font_cn_16.c",
            "src/ui_font_cn_24.c",
        ):
            self.assertTrue((canonical / relative).is_file())

        for project_name in RICH_PLATFORM_CLIENT_PROJECTS:
            component = ROOT / project_name / "components/starter_product"
            cmake = (component / "CMakeLists.txt").read_text()
            self.assertIn("platforms/esp-idf/components/starter_product", cmake)
            self.assertIn("XIAOTAI_STARTER_PRODUCT_ASSET_DIR", cmake)
            self.assertFalse((component / "src/starter_product.c").exists())
            self.assertIn("product/assets/audio", cmake)
            self.assertNotIn("lckfb-szpi-esp32s3-tirtc/components/starter_product/assets", cmake)

        szpi_cmake = (SZPI_PROJECT / "components/starter_product/CMakeLists.txt").read_text()
        self.assertIn("board_display.c", szpi_cmake)
        for project_name in RICH_PLATFORM_CLIENT_PROJECTS[1:]:
            cmake = (
                ROOT / project_name / "components/starter_product/CMakeLists.txt"
            ).read_text()
            self.assertIn("XIAOTAI_STARTER_PRODUCT_EXTRA_REQUIRES p4_hardware", cmake)

    def test_repository_owns_shared_host_and_i2c_gates(self) -> None:
        host_runner = ROOT / "tools/run_host_tests.sh"
        i2c_gate = ROOT / "tools/check_esp_idf_i2c_driver_family.sh"
        self.assertTrue(host_runner.is_file())
        self.assertTrue(i2c_gate.is_file())

        legacy_runner = (SZPI_PROJECT / "tools/run_host_tests.sh").read_text()
        self.assertIn('exec bash "$repo_dir/tools/run_host_tests.sh"', legacy_runner)

        for project_name in RICH_PLATFORM_CLIENT_PROJECTS:
            cmake = (ROOT / project_name / "CMakeLists.txt").read_text()
            self.assertIn("tools/check_esp_idf_i2c_driver_family.sh", cmake)
            legacy_name = "tools/check_i2c_" + "driver_family.sh"
            self.assertNotIn(legacy_name, cmake)

    def test_p4_downlink_renderer_is_family_owned(self) -> None:
        family = ROOT / "platforms/esp-idf/waveshare_p4"
        self.assertTrue((family / "call_video_renderer.c").is_file())
        self.assertTrue((family / "call_video_renderer.h").is_file())
        source_set = (family / "component_sources.cmake").read_text()
        self.assertIn('${xiaotai_platform}/call_video_renderer.c', source_set)
        self.assertNotIn('${ref}/services/call_video_renderer.c', source_set)

        for project_name in RICH_PLATFORM_CLIENT_PROJECTS[1:]:
            services = ROOT / project_name / "main/services"
            self.assertFalse((services / "call_video_renderer.c").exists())
            self.assertFalse((services / "call_video_renderer.h").exists())
            config = (services / "call_video_renderer_config.h").read_text()
            self.assertIn("CALL_VIDEO_REQUIRE_MATCHING_ASPECT", config)
            self.assertIn("CALL_VIDEO_H264_FIT_COVER", config)
            self.assertIn("CALL_VIDEO_MJPEG_ADAPTIVE_PLAYOUT", config)

    def test_p4_legacy_product_tree_cannot_reenter_the_build(self) -> None:
        """Keep the retained pre-migration tree as reference-only code.

        Both P4 directories still contain their original product implementation
        while board HIL is pending.  The active build must remain the canonical
        composition root plus the explicit family/board media source set; a
        broad source glob would silently restore a second product implementation.
        """
        family_sources = (
            ROOT / "platforms/esp-idf/waveshare_p4/component_sources.cmake"
        ).read_text()
        for relative in (
            "drivers/display/display_driver.c",
            "media/camera_pipeline.c",
            "media/media_governor.c",
        ):
            self.assertIn(f'${{ref}}/{relative}', family_sources)

        forbidden_fragments = (
            "${ref}/application/",
            "${ref}/connectivity/",
            "${ref}/protocols/",
            "${ref}/services/ai_chat",
            "${ref}/services/device",
            "${ref}/services/rtc_media_bridge",
            "${ref}/services/wechat_voip/",
            "${ref}/ui/",
        )
        for fragment in forbidden_fragments:
            self.assertNotIn(fragment, family_sources)

        for project_name in RICH_PLATFORM_CLIENT_PROJECTS[1:]:
            project = ROOT / project_name
            main_cmake = (project / "main/CMakeLists.txt").read_text()
            self.assertIn("XIAOTAI_MAIN_EXTRA_REQUIRES p4_hardware", main_cmake)
            self.assertIn("platforms/esp-idf/main/CMakeLists.txt", main_cmake)
            self.assertNotIn("GLOB", main_cmake.upper())

            p4_component = (
                project / "components/p4_hardware/CMakeLists.txt"
            ).read_text()
            self.assertNotIn("GLOB", p4_component.upper())
            self.assertIn(
                'SRCS "p4_video.c" ${XIAOTAI_P4_FAMILY_SRCS} '
                "${XIAOTAI_P4_PROJECT_MEDIA_SRCS}",
                p4_component,
            )


if __name__ == "__main__":
    unittest.main()
