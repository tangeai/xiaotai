#!/usr/bin/env python3
"""Unified XiaoTai board build dispatcher."""

from __future__ import annotations

import argparse
import os
from pathlib import Path
import shlex
import subprocess
import sys

from board_registry import ManifestError, build_command, find_board, load_boards
from firmware_version import bump_board_build


def build_environment(release: bool,
                      base: dict[str, str] | None = None) -> dict[str, str]:
    """Return a clean build environment with an explicit profile."""
    environment = dict(os.environ if base is None else base)
    environment.pop("XIAOTAI_BUILD_PROFILE", None)
    if release:
        environment["XIAOTAI_BUILD_PROFILE"] = "release"
    return environment


def prepare_dependency_lock(project_dir: Path) -> bool:
    """Expand portable project-local paths before ESP-IDF configures.

    The component manager resolves relative local sources from its CMake build
    directory, not from the project directory.  Give it an unambiguous path
    for the build, then normalize the lock again after the command finishes.
    """
    lock = project_dir / "dependencies.lock"
    if not lock.is_file():
        return False
    contents = lock.read_text(encoding="utf-8")
    prepared = contents.replace("path: $PWD/", f"path: {project_dir.resolve()}/")
    if prepared == contents:
        return False
    lock.write_text(prepared, encoding="utf-8")
    return True


def normalize_dependency_lock(project_dir: Path) -> bool:
    """Remove checkout-specific project prefixes written by ESP-IDF.

    The component manager resolves ``$PWD`` local component sources while it
    configures a project and writes the absolute result back to
    ``dependencies.lock``.  Keeping that generated prefix would make a clean
    build dirty on every checkout and would break another developer's clone.
    """
    lock = project_dir / "dependencies.lock"
    if not lock.is_file():
        return False
    contents = lock.read_text(encoding="utf-8")
    prefix = f"{project_dir.resolve()}/"
    normalized = contents.replace(f"path: {prefix}", "path: $PWD/")
    if normalized == contents:
        return False
    lock.write_text(normalized, encoding="utf-8")
    return True


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    action = parser.add_mutually_exclusive_group(required=True)
    action.add_argument("--list-boards", action="store_true", help="list registered boards")
    action.add_argument("--validate", action="store_true", help="validate manifests")
    action.add_argument("--board", metavar="ID", help="build one registered board")
    action.add_argument("--all", action="store_true", help="build every registered board")
    parser.add_argument("--variant", help="build variant; defaults to the manifest default")
    parser.add_argument("--dry-run", action="store_true", help="print the selected command only")
    parser.add_argument(
        "--release",
        action="store_true",
        help="formal build: increment this board's +build.n before compiling",
    )
    parser.add_argument(
        "--keep-going",
        action="store_true",
        help="with --all, continue after a board build fails",
    )
    return parser.parse_args()


def print_build(root: Path, board, variant: str) -> tuple[list[str], Path]:
    command, cwd = build_command(root, board, variant)
    variant_data = next(item for item in board.data["variants"] if item["name"] == variant)
    print(f"board: {board.id}")
    print(f"variant: {variant}")
    print(f"interaction: {variant_data['interaction_profile']}")
    print(f"layout: {variant_data['layout_profile']}")
    print(f"project: {cwd.relative_to(root)}")
    print(f"command: {shlex.join(command)}")
    return command, cwd


def main() -> int:
    args = parse_args()
    root = Path(__file__).resolve().parents[1]
    try:
        boards = load_boards(root)
        if args.list_boards:
            for board in boards:
                variants = ",".join(
                    f"{item['name']}:{item['interaction_profile']}/{item['layout_profile']}"
                    for item in board.data["variants"]
                )
                print(
                    f"{board.id:<42} {board.data['platform']:<7} {board.data['target']:<8} "
                    f"variants={variants} project={board.project_dir}"
                )
            return 0
        if args.validate:
            print(f"validated {len(boards)} board manifests")
            return 0

        selected = boards if args.all else [find_board(boards, args.board)]
        if args.all and args.variant:
            raise ManifestError("--variant can only be used with --board")
        if args.release and args.dry_run:
            raise ManifestError("--release cannot be combined with --dry-run")

        failures: list[tuple[str, int]] = []
        for index, board in enumerate(selected):
            if index:
                print()
            variant = args.variant or board.default_variant()
            if args.release:
                version = bump_board_build(root, board.id)
                print(f"firmware-version: {version}")
            command, cwd = print_build(root, board, variant)
            if args.dry_run:
                continue
            if board.data["platform"] == "esp-idf":
                prepare_dependency_lock(cwd)
            try:
                return_code = subprocess.run(
                    command,
                    cwd=cwd,
                    check=False,
                    env=build_environment(args.release),
                ).returncode
            finally:
                if board.data["platform"] == "esp-idf" and normalize_dependency_lock(cwd):
                    print(f"normalized: {cwd.relative_to(root) / 'dependencies.lock'}")
            if return_code:
                failures.append((board.id, return_code))
                if not args.keep_going:
                    return return_code

        if failures:
            print("failed boards:", file=sys.stderr)
            for board_id, return_code in failures:
                print(f"  {board_id}: exit {return_code}", file=sys.stderr)
            return 1
        return 0
    except ManifestError as exc:
        print(f"build selection failed: {exc}", file=sys.stderr)
        return 2
    except FileNotFoundError as exc:
        print(f"build tool not found: {exc.filename}", file=sys.stderr)
        return 127


if __name__ == "__main__":
    raise SystemExit(main())
