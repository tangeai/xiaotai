"""Keep the Waveshare 4.3-inch audio gate anchored to the migrated board sources."""

from __future__ import annotations

import importlib.util
import sys
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "tools"))

from board_registry import find_board, load_boards  # noqa: E402


BOARDS = load_boards(ROOT)
PROJECT = find_board(
    BOARDS, "waveshare-esp32p4-touch-lcd-43c-v10"
).project_path(ROOT)
VERIFIER = PROJECT / "tools/verify_audio_contract.py"
SPEC = importlib.util.spec_from_file_location("p4_audio_contract", VERIFIER)
assert SPEC is not None and SPEC.loader is not None
MODULE = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(MODULE)


class WaveshareP4AudioContractTest(unittest.TestCase):
    def test_project_selects_shared_board_implementation(self) -> None:
        family_sources = ROOT / "platforms/esp-idf/waveshare_p4/component_sources.cmake"
        source_set = family_sources.read_text()
        self.assertIn('"${xiaotai_platform}/hardware_board.c"', source_set)
        self.assertIn('"${xiaotai_platform}/camera_driver.c"', source_set)
        self.assertIn('"${xiaotai_platform}/app_memory_policy.c"', source_set)
        cmake = (PROJECT / "components/p4_hardware/CMakeLists.txt").read_text()
        self.assertIn('include("${xiaotai_platform}/component_sources.cmake")', cmake)
        self.assertIn("${XIAOTAI_P4_FAMILY_SRCS}", cmake)
        self.assertFalse((PROJECT / "main/hardware/hardware_board.c").exists())
        self.assertFalse((PROJECT / "main/drivers/camera/camera_driver.c").exists())
        self.assertFalse((PROJECT / "main/platform/app_memory_policy.c").exists())
        compatibility_header = PROJECT / "main/hardware/hardware_board.h"
        self.assertIn(
            "platforms/esp-idf/waveshare_p4/hardware_board.h",
            compatibility_header.read_text(),
        )
        for header_name in ("hardware_board.h", "hardware_board_config.h"):
            header = PROJECT / "main/hardware" / header_name
            include_line = next(
                line for line in header.read_text().splitlines()
                if line.startswith("#include \"")
            )
            include_target = include_line.split('"')[1]
            self.assertTrue(
                (header.parent / include_target).resolve().is_file(),
                f"broken compatibility include: {header} -> {include_target}",
            )

    def test_wechat_voip_uses_protocol_audio_stream_zero(self) -> None:
        source = (
            PROJECT / "main/services/wechat_voip/wechat_voip_media.c"
        ).read_text(encoding="utf-8")
        self.assertIn("#define WECHAT_VOIP_AUDIO_STREAM_ID       0U", source)
        self.assertNotIn("#define WECHAT_VOIP_AUDIO_STREAM_ID       10U", source)

    def test_board_config_and_shared_adapter_pass_semantic_gate(self) -> None:
        result = MODULE.verify_contract(PROJECT / "board-audio-contract.json", PROJECT)
        self.assertTrue(result["ok"], result["errors"])
        self.assertIn(
            "@repo/boards/waveshare/esp32p4-touch-lcd-43c/board_config.h",
            result["inputs"],
        )
        self.assertIn(
            "@repo/platforms/esp-idf/waveshare_p4/hardware_board.c",
            result["inputs"],
        )

    def test_repo_source_cannot_escape_workspace(self) -> None:
        with self.assertRaises(ValueError):
            MODULE.project_file(PROJECT, "@repo/../outside.c", "test.file")


if __name__ == "__main__":
    unittest.main()
