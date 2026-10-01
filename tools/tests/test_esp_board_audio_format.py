"""Board audio formats and lifecycle must remain platform neutral."""

from __future__ import annotations

import sys
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "tools"))

from board_registry import find_board, load_boards  # noqa: E402


BOARDS = load_boards(ROOT)
ATK_PROJECT = find_board(BOARDS, "alientek-atk-dnesp32s3").project_path(ROOT)
BK7258_PROJECT = find_board(BOARDS, "lckfb-bk7258").project_path(ROOT)
SZPI_PROJECT = find_board(BOARDS, "lckfb-esp32s3").project_path(ROOT)
P4_PROJECTS = tuple(
    find_board(BOARDS, board_id).project_path(ROOT)
    for board_id in (
        "waveshare-esp32p4-touch-lcd-43c-v10",
    )
)


class BoardAudioFormatTest(unittest.TestCase):
    def test_format_contract_is_platform_neutral(self) -> None:
        contract = (
            ROOT / "platforms/common/include/xiaotai_board_audio.h"
        ).read_text()
        self.assertIn("XIAOTAI_CAPTURE_MONO_MIC", contract)
        self.assertIn("xiaotai_board_audio_format_is_valid", contract)
        self.assertIn("int (*request_stop)(void *context);", contract)
        self.assertIn("int (*stop)(void *context);", contract)
        self.assertFalse(
            (ROOT / "platforms/esp-idf/include/xiaotai_board_audio.h").exists()
        )

    def test_atk_duplicated_microphone_layout(self) -> None:
        board = (ROOT / "boards/alientek/atk-dnesp32s3/atk_board.c").read_text()
        media = (ATK_PROJECT / "components/starter_media/src/starter_media.c").read_text()
        self.assertIn(".capture_layout = XIAOTAI_CAPTURE_DUPLICATED_MIC", board)
        self.assertIn("atk_board_audio_adapter()", media)
        self.assertIn("s_audio_adapter->format", media)
        self.assertIn("audio_format.capture_layout != XIAOTAI_CAPTURE_DUPLICATED_MIC", media)

    def test_szpi_microphone_reference_layout(self) -> None:
        board = (ROOT / "boards/lckfb/esp32s3/board_adapter.c").read_text()
        media = (SZPI_PROJECT / "components/starter_media/src/starter_media.c").read_text()
        self.assertIn(".capture_layout = XIAOTAI_CAPTURE_MIC_PLAYBACK_REFERENCE", board)
        self.assertIn("szpi_board_audio_adapter()", media)
        self.assertIn("s_audio_adapter->format", media)
        self.assertIn("XIAOTAI_CAPTURE_MIC_PLAYBACK_REFERENCE", media)

    def test_waveshare_p4_projects_use_common_audio_operations(self) -> None:
        for project in P4_PROJECTS:
            cmake = (project / "components/starter_media/CMakeLists.txt").read_text()
            media = (
                project / "components/starter_media/src/starter_media.c"
            ).read_text()
            self.assertIn("platforms/common/include", cmake)
            self.assertIn("xiaotai_board_audio_adapter_t", media)
            self.assertIn("XIAOTAI_CAPTURE_MIC_PLAYBACK_REFERENCE", media)
            self.assertIn("s_audio_adapter->read_pcm", media)
            self.assertIn("s_audio_adapter->write_pcm", media)

    def test_bk7258_uses_common_two_phase_audio_lifecycle(self) -> None:
        board = (
            ROOT / "boards/lckfb/bk7258/board_audio.c"
        ).read_text()
        pipeline = (BK7258_PROJECT / "ap/src/xiaotai_audio.c").read_text()
        cmake = (BK7258_PROJECT / "ap/CMakeLists.txt").read_text()
        self.assertIn("xiaotai_board_audio_adapter_t", board)
        self.assertIn(".capture_layout = XIAOTAI_CAPTURE_MONO_MIC", board)
        self.assertIn(".request_stop = audio_adapter_request_stop", board)
        self.assertIn(".stop = audio_adapter_stop", board)
        self.assertIn("s_board_audio->read_pcm", pipeline)
        self.assertIn("s_board_audio->write_pcm", pipeline)
        self.assertIn("s_board_audio->request_stop", pipeline)
        self.assertNotIn("audio_record_create(", pipeline)
        self.assertNotIn("audio_play_create(", pipeline)
        self.assertIn("platforms/common", cmake)

    def test_s3_camera_contract_hides_esp_camera_frame_type(self) -> None:
        contract = (
            ROOT / "platforms/common/include/xiaotai_board_camera.h"
        ).read_text()
        self.assertIn("xiaotai_board_camera_adapter_t", contract)
        self.assertIn("void *token", contract)
        for header_path, media_path, accessor in (
            (
                "boards/alientek/atk-dnesp32s3/atk_board.h",
                str(
                    ATK_PROJECT.relative_to(ROOT)
                    / "components/starter_media/src/starter_media.c"
                ),
                "atk_board_camera_adapter()",
            ),
            (
                "boards/lckfb/esp32s3/board_adapter.h",
                str(
                    SZPI_PROJECT.relative_to(ROOT)
                    / "components/starter_media/src/starter_media.c"
                ),
                "szpi_board_camera_adapter()",
            ),
        ):
            header = (ROOT / header_path).read_text()
            media = (ROOT / media_path).read_text()
            self.assertNotIn("camera_fb_t", header)
            self.assertIn(accessor, media)
            self.assertIn("s_camera_adapter->acquire", media)
            self.assertIn("s_camera_adapter->release", media)


if __name__ == "__main__":
    unittest.main()
