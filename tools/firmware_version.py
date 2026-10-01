"""Firmware version policy and board-specific source updates."""

from __future__ import annotations

from dataclasses import dataclass
from pathlib import Path
import re


VERSION_PATTERN = re.compile(
    r"(?P<major>0|[1-9][0-9]*)\."
    r"(?P<minor>0|[1-9][0-9]*)\."
    r"(?P<patch>0|[1-9][0-9]*)\+build\."
    r"(?P<build>[1-9][0-9]*)"
)


@dataclass(frozen=True)
class FirmwareVersion:
    major: int
    minor: int
    patch: int
    build: int

    @classmethod
    def parse(cls, value: str) -> "FirmwareVersion":
        match = VERSION_PATTERN.fullmatch(value)
        if match is None:
            raise ValueError(
                f"invalid firmware version {value!r}; expected x.y.z+build.n"
            )
        return cls(*(int(match.group(name)) for name in
                     ("major", "minor", "patch", "build")))

    def bump_build(self) -> "FirmwareVersion":
        return FirmwareVersion(self.major, self.minor, self.patch,
                               self.build + 1)

    def __str__(self) -> str:
        return (f"{self.major}.{self.minor}.{self.patch}"
                f"+build.{self.build}")


def _replace_one(path: Path, pattern: str, replacement: str) -> None:
    contents = path.read_text(encoding="utf-8")
    updated, count = re.subn(pattern, replacement, contents, count=1)
    if count != 1:
        raise ValueError(f"cannot locate firmware version in {path}")
    path.write_text(updated, encoding="utf-8")


def _cmake_version(path: Path) -> FirmwareVersion:
    contents = path.read_text(encoding="utf-8")
    match = re.search(r'set\(PROJECT_VER\s+"([^"]+)"\)', contents)
    if match is None:
        raise ValueError(f"cannot locate PROJECT_VER in {path}")
    return FirmwareVersion.parse(match.group(1))


def _replace_profile_version(path: Path, board_model: str,
                             current: FirmwareVersion,
                             updated: FirmwareVersion) -> None:
    contents = path.read_text(encoding="utf-8")
    marker = f'board_model\\\":\\\"{board_model}'
    start = contents.find(marker)
    if start < 0:
        marker = f'board_model":"{board_model}'
        start = contents.find(marker)
    if start < 0:
        raise ValueError(f"cannot locate profile for {board_model} in {path}")
    version_at = contents.find(str(current), start, start + 320)
    if version_at < 0:
        raise ValueError(f"cannot locate {current} for {board_model} in {path}")
    contents = (contents[:version_at] + str(updated) +
                contents[version_at + len(str(current)):])
    path.write_text(contents, encoding="utf-8")


def bump_board_build(root: Path, board_id: str) -> FirmwareVersion:
    """Increment one board's formal-build number without changing x.y.z."""
    projects = {
        "alientek-atk-dnesp32s3":
            "firmware/esp-idf/esp32s3/atk-dnesp32s3",
        "lckfb-esp32s3":
            "firmware/esp-idf/esp32s3/lckfb-esp32s3-xiaotai",
        "waveshare-esp32p4-touch-lcd-43c-v10":
            "firmware/esp-idf/esp32p4/waveshare-touch-lcd-43c",
    }
    if board_id == "lckfb-bk7258":
        project = root / "firmware/beken/bk7258/lckfb-bk7258-xiaotai"
        version_file = project / "VERSION.md"
        match = re.search(r"`([^`]+)`", version_file.read_text(encoding="utf-8"))
        if match is None:
            raise ValueError(f"cannot locate firmware version in {version_file}")
        current = FirmwareVersion.parse(match.group(1))
        updated = current.bump_build()
        _replace_one(version_file, re.escape(str(current)), str(updated))
        _replace_one(project / "ap/src/xiaotai_platform_client.c",
                     re.escape(str(current)), str(updated))
        return updated

    relative = projects.get(board_id)
    if relative is None:
        raise ValueError(f"no firmware version source registered for {board_id}")
    cmake = root / relative / "CMakeLists.txt"
    current = _cmake_version(cmake)
    updated = current.bump_build()
    _replace_one(cmake, re.escape(str(current)), str(updated))

    shared_profile = root / "platforms/esp-idf/components/starter_runtime/src/starter_runtime.c"
    profile_models = {
        "lckfb-esp32s3": "lckfb-esp32s3",
        "waveshare-esp32p4-touch-lcd-43c-v10":
            "waveshare-esp32p4-touch-lcd-43c-v10",
    }
    if board_id in profile_models:
        _replace_profile_version(shared_profile, profile_models[board_id],
                                 current, updated)
    return updated
