"""Compile and execute the platform-neutral C product-core contracts."""

from __future__ import annotations

from pathlib import Path
import subprocess
import tempfile
import unittest


ROOT = Path(__file__).resolve().parents[2]
PRODUCT = ROOT / "product"


class ProductCoreTest(unittest.TestCase):
    def compile_and_run(
        self,
        name: str,
        source: str | list[str],
        test: str,
        *,
        cjson: bool = False,
    ) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            output = Path(tmp) / name
            command = [
                "cc",
                "-std=c11",
                "-Wall",
                "-Wextra",
                "-Werror",
                "-I",
                str(PRODUCT / "include"),
            ]
            if cjson:
                command.extend(["-I", "/usr/include/cjson"])
            sources = [source] if isinstance(source, str) else source
            command.extend(str(PRODUCT / "src" / item) for item in sources)
            command.append(str(PRODUCT / "tests" / test))
            if cjson:
                command.append("-lcjson")
            command.extend(["-o", str(output)])
            subprocess.run(command, check=True)
            subprocess.run([str(output)], check=True)

    def test_runtime(self) -> None:
        self.compile_and_run(
            "runtime", "xiaotai_runtime.c", "test_runtime.c"
        )

    def test_call_protocol(self) -> None:
        self.compile_and_run(
            "call_protocol",
            "xiaotai_call_protocol.c",
            "test_call_protocol.c",
            cjson=True,
        )

    def test_call_state(self) -> None:
        self.compile_and_run(
            "call_state", "xiaotai_call_state.c", "test_call_state.c"
        )

    def test_call_state_resilience(self) -> None:
        self.compile_and_run(
            "call_state_resilience",
            "xiaotai_call_state.c",
            "test_call_state_resilience.c",
        )

    def test_contacts(self) -> None:
        self.compile_and_run(
            "contacts", "xiaotai_contacts.c", "test_contacts.c", cjson=True
        )

    def test_ai_view(self) -> None:
        self.compile_and_run(
            "ai_view", "xiaotai_ai_view.c", "test_ai_view.c", cjson=True
        )

    def test_ai_protocol(self) -> None:
        self.compile_and_run(
            "ai_protocol",
            "xiaotai_ai_protocol.c",
            "test_ai_protocol.c",
            cjson=True,
        )

    def test_video_pacer(self) -> None:
        self.compile_and_run(
            "video_pacer", "xiaotai_video_pacer.c", "test_video_pacer.c"
        )

    def test_room(self) -> None:
        self.compile_and_run(
            "room", "xiaotai_room.c", "test_room.c", cjson=True
        )

    def test_room_resilience(self) -> None:
        self.compile_and_run(
            "room_resilience",
            "xiaotai_room.c",
            "test_room_resilience.c",
            cjson=True,
        )

    def test_signal(self) -> None:
        self.compile_and_run(
            "signal", "xiaotai_signal.c", "test_signal.c", cjson=True
        )

    def test_media_contract(self) -> None:
        self.compile_and_run(
            "media_contract",
            "xiaotai_media_contract.c",
            "test_media_contract.c",
        )

    def test_business_scenarios(self) -> None:
        self.compile_and_run(
            "business_scenarios",
            [
                "xiaotai_runtime.c",
                "xiaotai_ai_protocol.c",
                "xiaotai_call_state.c",
                "xiaotai_contacts.c",
                "xiaotai_signal.c",
                "xiaotai_media_contract.c",
            ],
            "test_business_scenarios.c",
            cjson=True,
        )


if __name__ == "__main__":
    unittest.main()
