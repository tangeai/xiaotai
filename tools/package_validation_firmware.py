#!/usr/bin/env python3
"""Package every registered XiaoTai board build for hardware validation."""

from __future__ import annotations

import argparse
import json
import re
import shutil
import zipfile
from datetime import datetime, timezone
from pathlib import Path

from board_registry import Board, find_board, load_boards
from firmware_version import FirmwareVersion
from package_esp_validation import (add_file, git_output, package_board, sha256,
                                    VERIFICATION_STATUSES, verification_readme_status)



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
                  build_profile: str = "development",
                  verification_status: str = "BUILD_VERIFIED_HIL_PENDING") -> Path:
    project_dir = board.project_path(root)
    target = board.data["target"]
    build_root = beken_build_root(project_dir, target, build_profile)
    package_dir = build_root / "package"
    application = package_dir / "all-app.bin"
    ota_image = package_dir / "app_pack.rbl"
    ap_elf = build_root / f"{target}_ap" / "app.elf"
    identity_file = build_root / "build_identity.json"
    cp_elf = build_root / target / "app.elf"
    partition_dir = build_root / "partitions"
    version_file = project_dir / "VERSION.md"
    version_match = re.search(r"`([^`]+)`",
                              version_file.read_text(encoding="utf-8"))
    if version_match is None:
        raise ValueError(f"missing firmware version in {version_file}")
    version = str(FirmwareVersion.parse(version_match.group(1)))
    records: list[dict[str, object]] = []

    required = (
        application,
        ota_image,
        ap_elf,
        cp_elf,
        package_dir / "build_summary.txt",
        partition_dir / "bk_package.json",
        partition_dir / "partitions.json",
        identity_file,
    )
    missing = [str(path) for path in required if not path.is_file()]
    if missing:
        raise FileNotFoundError("missing Beken build artifacts: " + ", ".join(missing))

    identity = json.loads(identity_file.read_text(encoding="utf-8"))
    if identity.get('build_profile') != build_profile:
        raise ValueError('BK build profile does not match the requested profile')
    if identity["version"] != version or identity["all_app_sha256"] != sha256(application):
        raise ValueError("BK build identity does not match the flash image")
    full_version = f'{version}+build.{identity["build_id"]}'
    if (not str(identity['build_id']).isdigit() or
            identity.get('build_version') != full_version):
        raise ValueError('BK build version is invalid')
    if full_version.encode() not in ap_elf.read_bytes():
        raise ValueError("BK AP ELF does not contain the packaged build ID")
    flash_name = f'{board.id}-{full_version}-all-app.bin'
    ota_name = f'{board.id}-{full_version}-app_pack.rbl'
    zip_path = output_dir / f"{board.id}-{full_version}-validation.zip"

    source_commit = git_output(root, "rev-parse", "HEAD")
    source_dirty = bool(git_output(root, "status", "--porcelain"))
    with zipfile.ZipFile(zip_path, "w", compression=zipfile.ZIP_DEFLATED) as archive:
        add_file(archive, application, flash_name, records)
        add_file(archive, ota_image, ota_name, records)
        add_file(archive, ap_elf, f"symbols/{target}_ap.elf", records)
        add_file(archive, cp_elf, f"symbols/{target}_cp.elf", records)
        add_file(archive, package_dir / "build_summary.txt", "build_summary.txt", records)
        add_file(archive, partition_dir / "bk_package.json", "partitions/bk_package.json", records)
        add_file(archive, partition_dir / "partitions.json", "partitions/partitions.json", records)
        add_file(archive, identity_file, "build_identity.json", records)

        manifest = {
            "schema_version": 1,
            "board_id": board.id,
            "project_version": full_version,
            "build_id": identity["build_id"],
            "build_profile": build_profile,
            "purpose": identity.get('purpose', ''),
            "platform": "beken",
            "target": target,
            "verification_status": verification_status,
            "source_commit": source_commit,
            "source_worktree_dirty": source_dirty,
            "generated_at": datetime.now(timezone.utc).isoformat(),
            "sdk": {
                "name": board.data["sdk"]["name"],
                "version": board.data["sdk"]["version"],
            },
            "application": {
                "path": flash_name,
                "size_bytes": application.stat().st_size,
                "sha256": sha256(application),
                "flash_offset": "0x00000000",
            },
            "ota_image": {
                "path": ota_name,
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
            "README.md",
            "XiaoTai BK7258 validation firmware\n"
            f"Board: {board.data['vendor']} {board.data['model']}\n"
            f"Version: {full_version}\n"
            f"Build: {identity['build_id']}\n"
            f"Status: {verification_readme_status(verification_status)}\n\n"
            f"Flash {flash_name} at offset 0x00000000 with the "
            f"board vendor's {target.upper()} downloader. Preserve RF calibration and "
            f"factory-data partitions. {ota_name} is the OTA image, not the "
            "initial full-flash image.\n\n"
            "Both AP and CP ELF files are included for crash symbolization.\n",
        )
    shutil.copyfile(application, output_dir / flash_name)
    shutil.copyfile(ota_image, output_dir / ota_name)
    (output_dir / 'README.md').write_text(
        '# BK7258 firmware\n\n'
        f'Version: `{full_version}`\n\n'
        f'Profile: `{build_profile}`\n\n'
        f'Purpose: {identity.get("purpose", "")}\n\n'
        f'Status: {verification_readme_status(verification_status)}\n\n'
        f'Flash file: [{flash_name}]({flash_name}) at `0x00000000`.\n\n'
        f'SHA-256: `{sha256(application)}`\n\n'
        f'Original SDK image: `{application.resolve()}`. Files here are copies.\n',
        encoding='utf-8')
    return zip_path


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--output", help="explicit export directory; defaults to the board project's build directory")
    parser.add_argument(
        "--board",
        help="package one registered board; defaults to every registered board",
    )
    parser.add_argument(
        "--beken-build-profile",
        choices=("release", "development"),
        default="development",
        help="Beken artifact tree to package (default: development; release is for online delivery)",
    )
    parser.add_argument(
        "--verification-status",
        choices=VERIFICATION_STATUSES,
        default="BUILD_VERIFIED_HIL_PENDING",
        help="artifact verification evidence recorded in the package manifest",
    )
    args = parser.parse_args()

    root = Path(__file__).resolve().parents[1]
    boards = load_boards(root)
    selected = [find_board(boards, args.board)] if args.board else boards
    archives = []
    for board in selected:
        if args.output:
            output_dir = (root / args.output).resolve()
        elif board.data['platform'] == 'beken':
            output_dir = beken_build_root(board.project_path(root),
                                          board.data['target'],
                                          args.beken_build_profile) / 'package'
        else:
            output_dir = board.project_path(root) / 'build'
        output_dir.mkdir(parents=True, exist_ok=True)
        if board.data["platform"] == "esp-idf":
            archives.append(
                package_board(root, board.project_path(root), output_dir, board.id,
                              verification_status=args.verification_status)
            )
        elif board.data["platform"] == "beken":
            archives.append(
                package_beken(
                    root,
                    board,
                    output_dir,
                    args.beken_build_profile,
                    args.verification_status,
                )
            )
    for directory in {path.parent for path in archives}:
        sums = "".join(f"{sha256(path)}  {path.name}\n"
                       for path in archives if path.parent == directory)
        (directory / "SHA256SUMS").write_text(sums, encoding="utf-8")
    for path in archives:
        print(path)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
