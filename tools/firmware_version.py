"""Read user-selected firmware versions without changing them during builds."""

from __future__ import annotations

from dataclasses import dataclass
from pathlib import Path
import re


VERSION_PATTERN = re.compile(
    r"(?P<major>0|[1-9][0-9]*)\."
    r"(?P<minor>0|[1-9][0-9]*)\."
    r"(?P<patch>0|[1-9][0-9]*)"
)


@dataclass(frozen=True)
class FirmwareVersion:
    major: int
    minor: int
    patch: int

    @classmethod
    def parse(cls, value: str) -> "FirmwareVersion":
        match = VERSION_PATTERN.fullmatch(value)
        if match is None:
            raise ValueError(f"invalid firmware version {value!r}; expected x.y.z")
        return cls(*(int(match.group(name)) for name in ("major", "minor", "patch")))

    def __str__(self) -> str:
        return f"{self.major}.{self.minor}.{self.patch}"


def read_board_version(root: Path, board_id: str) -> FirmwareVersion:
    """Return the selected board's version; builds never allocate versions."""
    projects = {
        "alientek-atk-dnesp32s3":
            "firmware/esp-idf/esp32s3/atk-dnesp32s3",
        "lckfb-esp32s3":
            "firmware/esp-idf/esp32s3/lckfb-esp32s3-xiaotai",
        "waveshare-esp32p4-touch-lcd-43c-v10":
            "firmware/esp-idf/esp32p4/waveshare-touch-lcd-43c",
    }
    if board_id == "lckfb-bk7258":
        path = root / "firmware/beken/bk7258/lckfb-bk7258-xiaotai/VERSION.md"
        match = re.search(r"`([^`]+)`", path.read_text(encoding="utf-8"))
    elif board_id in projects:
        path = root / projects[board_id] / "CMakeLists.txt"
        match = re.search(r'set\(PROJECT_VER\s+"([^"]+)"\)',
                          path.read_text(encoding="utf-8"))
    else:
        raise ValueError(f"no firmware version source registered for {board_id}")
    if match is None:
        raise ValueError(f"cannot locate firmware version in {path}")
    return FirmwareVersion.parse(match.group(1))
