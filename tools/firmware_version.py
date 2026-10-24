"""Read user-selected firmware versions without changing them during builds."""

from __future__ import annotations

from dataclasses import dataclass
from contextlib import contextmanager
import fcntl
from pathlib import Path
import re


VERSION_PATTERN = re.compile(
    r"(?P<major>0|[1-9][0-9]*)\."
    r"(?P<minor>0|[1-9][0-9]*)\."
    r"(?P<patch>0|[1-9][0-9]*)"
)


def read_build_number(project: Path) -> int | None:
    path = project / "BUILD_NUMBER"
    if not path.is_file():
        return None
    value = path.read_text(encoding="utf-8").strip()
    if not re.fullmatch(r"0|[1-9][0-9]*", value):
        raise ValueError(f"invalid build number in {path}")
    return int(value)


def esp_build_version(project: Path, product_version: str) -> str:
    version = str(FirmwareVersion.parse(product_version))
    number = read_build_number(project)
    if number is not None:
        if number < 1:
            raise ValueError("build number has not been allocated; use tools/build.py")
        version += f"+build.{number}"
    if len(version.encode()) > 31:
        raise ValueError("firmware identity exceeds ESP-IDF app descriptor capacity")
    return version


@contextmanager
def reserve_esp_build(project: Path, product_version: str):
    """Hold a project lock through compilation; a failed attempt consumes its ID."""
    if not (project / "BUILD_NUMBER").is_file():
        yield product_version
        return
    build_dir = project / "build"
    build_dir.mkdir(exist_ok=True)
    with (build_dir / ".xiaotai-build.lock").open("a") as lock:
        try:
            fcntl.flock(lock, fcntl.LOCK_EX | fcntl.LOCK_NB)
        except BlockingIOError as exc:
            raise ValueError(f"another build owns {project}") from exc
        number = read_build_number(project) + 1
        version = f"{FirmwareVersion.parse(product_version)}+build.{number}"
        if len(version.encode()) > 31:
            raise ValueError("firmware identity exceeds ESP-IDF app descriptor capacity")
        temporary = project / "BUILD_NUMBER.tmp"
        temporary.write_text(f"{number}\n", encoding="utf-8")
        temporary.replace(project / "BUILD_NUMBER")
        yield version


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
        "m5stack-cores3-lite":
            "firmware/esp-idf/esp32s3/m5stack-cores3-lite",
        "lckfb-esp32s3":
            "firmware/esp-idf/esp32s3/lckfb-esp32s3-xiaotai",
        "waveshare-esp32p4-touch-lcd-43c-v10":
            "firmware/esp-idf/esp32p4/waveshare-touch-lcd-43c",
    }
    if board_id in ("lckfb-bk7258", "mgkfb-bk7258"):
        path = root / f"firmware/beken/bk7258/{board_id}-xiaotai/VERSION.md"
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
