#!/usr/bin/env python3
"""Package every registered XiaoTai board build for hardware validation."""

from __future__ import annotations

import argparse
import json
import re
import zipfile
from datetime import datetime, timezone
from pathlib import Path

from board_registry import Board, find_board, load_boards
from firmware_version import FirmwareVersion
from package_esp_validation import add_file, git_output, package_board, sha256


def beken_build_root(project_dir: Path, target: str, profile: str) -> Path:
    """Resolve a Beken output tree without silently mixing build profiles."""
    if profile == "release":
        project_name = f"{project_dir.name}_release"
    elif profile == "development":
        project_name = project_dir.name
    else:
        raise ValueError(f"unsupported Beken build profile: {profile}")
    return project_dir / "build" / target / project_name


def package_beken(root: Path, board: Board, output_dir: Path,
                  build_profile: str = "release") -> Path:
    project_dir = board.project_path(root)
    target = board.data["target"]
    build_root = beken_build_root(project_dir, target, build_profile)
    package_dir = build_root / "package"
    application = package_dir / "all-app.bin"
    ota_image = package_dir / "app_pack.rbl"
    ap_elf = build_root / f"{target}_ap" / "app.elf"
    cp_elf = build_root / target / "app.elf"
    partition_dir = build_root / "partitions"
    version_file = project_dir / "VERSION.md"
    version_match = re.search(r"`([^`]+)`",
                              version_file.read_text(encoding="utf-8"))
    if version_match is None:
        raise ValueError(f"missing firmware version in {version_file}")
    version = str(FirmwareVersion.parse(version_match.group(1)))
    zip_path = output_dir / f"{board.id}-{version}-validation.zip"
    records: list[dict[str, object]] = []

    required = (
        application,
        ota_image,
        ap_elf,
        cp_elf,
        package_dir / "build_summary.txt",
        partition_dir / "bk_package.json",
        partition_dir / "partitions.json",
    )
    missing = [str(path) for path in required if not path.is_file()]
    if missing:
        raise FileNotFoundError("missing Beken build artifacts: " + ", ".join(missing))

    source_commit = git_output(root, "rev-parse", "HEAD")
    source_dirty = bool(git_output(root, "status", "--porcelain"))
    with zipfile.ZipFile(zip_path, "w", compression=zipfile.ZIP_DEFLATED) as archive:
        add_file(archive, application, "all-app.bin", records)
        add_file(archive, ota_image, "app_pack.rbl", records)
        add_file(archive, ap_elf, f"symbols/{target}_ap.elf", records)
        add_file(archive, cp_elf, f"symbols/{target}_cp.elf", records)
        add_file(archive, package_dir / "build_summary.txt", "build_summary.txt", records)
        add_file(archive, partition_dir / "bk_package.json", "partitions/bk_package.json", records)
        add_file(archive, partition_dir / "partitions.json", "partitions/partitions.json", records)

        manifest = {
            "schema_version": 1,
            "board_id": board.id,
            "project_version": version,
            "platform": "beken",
            "target": target,
            "verification_status": "BUILD_VERIFIED_HIL_PENDING",
            "source_commit": source_commit,
            "source_worktree_dirty": source_dirty,
            "generated_at": datetime.now(timezone.utc).isoformat(),
            "sdk": {
                "name": board.data["sdk"]["name"],
                "version": board.data["sdk"]["version"],
            },
            "application": {
                "path": "all-app.bin",
                "size_bytes": application.stat().st_size,
                "sha256": sha256(application),
                "flash_offset": "0x00000000",
            },
            "ota_image": {
                "path": "app_pack.rbl",
                "size_bytes": ota_image.stat().st_size,
                "sha256": sha256(ota_image),
            },
            "files": records,
        }
        archive.writestr(
            "MANIFEST.json",
            json.dumps(manifest, ensure_ascii=False, indent=2) + "\n",
        )
        archive.writestr(
            "README.txt",
            "XiaoTai BK7258 validation firmware\n"
            f"Board: {board.data['vendor']} {board.data['model']}\n"
            f"Version: {version}\n"
            "Status: build verified; this exact artifact still requires HIL.\n\n"
            "Flash the complete all-app.bin at offset 0x00000000 with the "
            f"board vendor's {target.upper()} downloader. Preserve RF calibration and "
            "factory-data partitions. app_pack.rbl is the OTA image, not the "
            "initial full-flash image.\n\n"
            "Both AP and CP ELF files are included for crash symbolization.\n",
        )
    return zip_path


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--output", default="output/validation")
    parser.add_argument(
        "--board",
        help="package one registered board; defaults to every registered board",
    )
    parser.add_argument(
        "--beken-build-profile",
        choices=("release", "development"),
        default="release",
        help="Beken artifact tree to package (default: release)",
    )
    args = parser.parse_args()

    root = Path(__file__).resolve().parents[1]
    output_dir = (root / args.output).resolve()
    output_dir.mkdir(parents=True, exist_ok=True)
    boards = load_boards(root)
    selected = [find_board(boards, args.board)] if args.board else boards
    archives = []
    for board in selected:
        if board.data["platform"] == "esp-idf":
            archives.append(
                package_board(root, board.project_path(root), output_dir, board.id)
            )
        elif board.data["platform"] == "beken":
            archives.append(
                package_beken(root, board, output_dir, args.beken_build_profile)
            )
    sums = "".join(f"{sha256(path)}  {path.name}\n" for path in archives)
    (output_dir / "SHA256SUMS").write_text(sums, encoding="utf-8")
    for path in archives:
        print(path)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
