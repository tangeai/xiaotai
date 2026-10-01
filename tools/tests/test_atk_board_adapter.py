"""ATK media contracts must inspect the registered board adapter source."""

from __future__ import annotations

import importlib.util
import sys
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "tools"))

from board_registry import find_board, load_boards  # noqa: E402


PROJECT = find_board(load_boards(ROOT), "alientek-atk-dnesp32s3").project_path(ROOT)
BOARD_SOURCE = "@repo/boards/alientek/atk-dnesp32s3/atk_board.c"
BOARD_CONFIG = "@repo/boards/alientek/atk-dnesp32s3/board_config.h"


def load_verifier(name: str):
    path = PROJECT / "tools" / f"verify_{name}_contract.py"
    spec = importlib.util.spec_from_file_location(f"atk_{name}_contract", path)
    assert spec is not None and spec.loader is not None
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


class AtkBoardAdapterTest(unittest.TestCase):
    def test_media_build_selects_board_directory(self) -> None:
        cmake = (PROJECT / "components/starter_media/CMakeLists.txt").read_text()
        self.assertIn('"${board_dir}/atk_board.c"', cmake)
        self.assertFalse((PROJECT / "components/starter_media/src/atk_board.c").exists())

    def test_audio_and_video_contracts_inspect_board_source(self) -> None:
        for name in ("audio", "video"):
            with self.subTest(name=name):
                verifier = load_verifier(name)
                result = verifier.verify_contract(
                    PROJECT / f"board-{name}-contract.json", PROJECT
                )
                self.assertTrue(result["ok"], result["errors"])
                self.assertIn(BOARD_SOURCE, result["inputs"])
                self.assertIn(BOARD_CONFIG, result["inputs"])
                with self.assertRaises(ValueError):
                    verifier.project_file(PROJECT, "@repo/../outside.c", "test.file")


if __name__ == "__main__":
    unittest.main()
