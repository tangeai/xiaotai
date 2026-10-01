"""Compile host conformance tests for public board driver interfaces."""

from pathlib import Path
import subprocess
import tempfile
import unittest


ROOT = Path(__file__).resolve().parents[2]


class BoardDriverContractsTest(unittest.TestCase):
    def test_audio_and_camera_contracts(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            output = Path(tmp) / "board_driver_contracts"
            subprocess.run(
                [
                    "cc",
                    "-std=c11",
                    "-Wall",
                    "-Wextra",
                    "-Werror",
                    "-I",
                    str(ROOT / "platforms/common/include"),
                    str(ROOT / "tests/driver/test_board_driver_contracts.c"),
                    "-o",
                    str(output),
                ],
                check=True,
            )
            subprocess.run([str(output)], check=True)


if __name__ == "__main__":
    unittest.main()
