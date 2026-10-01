"""Repository-location helpers shared by project-local validation tools."""

from pathlib import Path


def find_repository_root(start: Path) -> Path:
    candidate = start.resolve()
    for directory in (candidate, *candidate.parents):
        if (directory / "boards/schema/board.schema.json").is_file():
            return directory
    raise RuntimeError(f"cannot locate XiaoTai repository root from {start}")
