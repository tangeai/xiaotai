#!/usr/bin/env python3
"""Create a Beken release Kconfig with INFO as the maximum log level."""

from __future__ import annotations

import argparse
from pathlib import Path
import re


def prepare_release_config(source: Path, output: Path) -> None:
    contents = source.read_text(encoding="utf-8")
    updated, count = re.subn(
        r"^CONFIG_LOG_LEVEL=\d+$",
        "CONFIG_LOG_LEVEL=3",
        contents,
        flags=re.MULTILINE,
    )
    if count != 1:
        raise ValueError(
            f"expected exactly one CONFIG_LOG_LEVEL in {source}, found {count}"
        )
    output.write_text(updated, encoding="utf-8")


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("source", type=Path)
    parser.add_argument("output", type=Path)
    args = parser.parse_args()
    prepare_release_config(args.source, args.output)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
