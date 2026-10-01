#!/usr/bin/env python3
"""Validate every registered XiaoTai board manifest."""

from pathlib import Path
import sys

from board_registry import ManifestError, load_boards


def main() -> int:
    root = Path(__file__).resolve().parents[1]
    try:
        boards = load_boards(root)
    except ManifestError as exc:
        print(f"board validation failed:\n{exc}", file=sys.stderr)
        return 1
    print(f"validated {len(boards)} board manifests")
    for board in boards:
        print(f"  {board.id}: {board.data['platform']}/{board.data['target']} -> {board.project_dir}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
