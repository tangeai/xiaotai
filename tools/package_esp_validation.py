#!/usr/bin/env python3
"""Create self-contained ESP-IDF validation bundles from completed builds."""

from __future__ import annotations

import argparse
import hashlib
import json
import subprocess
import zipfile
from datetime import datetime, timezone
from pathlib import Path


def sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as handle:
        for chunk in iter(lambda: handle.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def git_output(root: Path, *args: str) -> str:
    return subprocess.check_output(
        ["git", *args], cwd=root, text=True, stderr=subprocess.DEVNULL
    ).strip()


def add_file(
    archive: zipfile.ZipFile,
    source: Path,
    archive_name: str,
    records: list[dict[str, object]],
) -> None:
    archive.write(source, archive_name)
    records.append(
        {
            "path": archive_name,
            "size_bytes": source.stat().st_size,
            "sha256": sha256(source),
        }
    )


def package_board(
    root: Path,
    project_dir: Path,
    output_dir: Path,
    board_id: str | None = None,
) -> Path:
    build_dir = project_dir / "build"
    flasher_path = build_dir / "flasher_args.json"
    description_path = build_dir / "project_description.json"
    flasher = json.loads(flasher_path.read_text(encoding="utf-8"))
    description = json.loads(description_path.read_text(encoding="utf-8"))
    board_id = board_id or project_dir.name
    version = description["project_version"]
    app_path = build_dir / flasher["app"]["file"]
    elf_path = build_dir / description["app_elf"]
    zip_path = output_dir / f"{board_id}-{version}-validation.zip"
    files: list[dict[str, object]] = []

    source_commit = git_output(root, "rev-parse", "HEAD")
    source_dirty = bool(git_output(root, "status", "--porcelain"))
    flash_files = [
        {"offset": offset, "file": relative}
        for offset, relative in sorted(
            flasher["flash_files"].items(), key=lambda item: int(item[0], 16)
        )
    ]
    flash_command = "esptool.py --chip {chip} write_flash {files}".format(
        chip=flasher["extra_esptool_args"]["chip"],
        files=" ".join(f"{item['offset']} {item['file']}" for item in flash_files),
    )

    with zipfile.ZipFile(zip_path, "w", compression=zipfile.ZIP_DEFLATED) as archive:
        for item in flash_files:
            relative = item["file"]
            add_file(archive, build_dir / relative, relative, files)
        add_file(archive, flasher_path, "flasher_args.json", files)
        add_file(archive, build_dir / "flash_args", "flash_args", files)
        add_file(archive, elf_path, elf_path.name, files)

        manifest = {
            "schema_version": 1,
            "board_id": board_id,
            "project_version": version,
            "verification_status": "BUILD_VERIFIED_HIL_PENDING",
            "source_commit": source_commit,
            "source_worktree_dirty": source_dirty,
            "generated_at": datetime.now(timezone.utc).isoformat(),
            "application": {
                "path": flasher["app"]["file"],
                "size_bytes": app_path.stat().st_size,
                "sha256": sha256(app_path),
            },
            "flash_files": flash_files,
            "flash_command": flash_command,
            "files": files,
        }
        archive.writestr(
            "MANIFEST.json",
            json.dumps(manifest, ensure_ascii=False, indent=2) + "\n",
        )
        archive.writestr(
            "README.txt",
            "XiaoTai ESP-IDF validation firmware\n"
            f"Board: {board_id}\nVersion: {version}\n"
            "Status: build verified; hardware validation pending.\n\n"
            "Run from this extracted directory:\n"
            f"  {flash_command}\n\n"
            "The ELF is included for crash-address symbolization.\n",
        )
    return zip_path


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument(
        "projects", nargs="+", help="ESP-IDF project directories relative to repo root"
    )
    parser.add_argument("--output", default="output/validation")
    args = parser.parse_args()

    root = Path(__file__).resolve().parents[1]
    output_dir = (root / args.output).resolve()
    output_dir.mkdir(parents=True, exist_ok=True)
    archives = [
        package_board(root, (root / project).resolve(), output_dir)
        for project in args.projects
    ]
    sums = "".join(f"{sha256(path)}  {path.name}\n" for path in archives)
    (output_dir / "SHA256SUMS").write_text(sums, encoding="utf-8")
    for path in archives:
        print(path)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
