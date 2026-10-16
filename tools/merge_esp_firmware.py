#!/usr/bin/env python3
"""Create a sibling merged BIN from an unchanged ESP-IDF validation ZIP.

Run in the project's ESP-IDF Python environment (provides esptool).
No build, flashing, Git push or GitHub upload is performed.
"""
from __future__ import annotations

import argparse
import hashlib
import json
from pathlib import Path, PurePosixPath
import re
import shutil
import subprocess
import sys
import tempfile
import zipfile


def merged_inputs(package: Path) -> tuple[str, str, list[tuple[int, str, bytes]]]:
    """Validate archive checksums and layout before invoking the vendor tool."""
    with zipfile.ZipFile(package) as archive:
        if len(archive.namelist()) != len(set(archive.namelist())):
            raise ValueError('duplicate archive entries')
        manifest = json.loads(archive.read('MANIFEST.json'))
        stem = f"{manifest['board_id']}-{manifest['project_version']}"
        if package.name != stem + '-validation.zip' or Path(stem).name != stem:
            raise ValueError('package name differs from firmware identity')
        records = manifest['files']
        verified = {}
        for record in records:
            name = record['path']
            path = PurePosixPath(name)
            if (path.is_absolute() or '..' in path.parts or '\\' in name or
                    str(path) != name or name in verified):
                raise ValueError('unsafe or duplicate archive path')
            data = archive.read(name)
            if (len(data) != record['size_bytes'] or
                    hashlib.sha256(data).hexdigest() != record['sha256']):
                raise ValueError(f'artifact checksum mismatch: {name}')
            verified[name] = data
        flasher = json.loads(verified['flasher_args.json'])
        chip = flasher['extra_esptool_args']['chip']
        if chip not in ('esp32s3', 'esp32p4') or manifest['image_identity']['target'] != chip:
            raise ValueError('unsupported or mismatched ESP target')
        if manifest['verification_status'] not in ('BUILD_VERIFIED_HIL_PENDING', 'HIL_VERIFIED'):
            raise ValueError('unsupported verification status')
        files = flasher['flash_files']
        listed = manifest['flash_files']
        if len(listed) != len(files) or {f['offset']: f['file'] for f in listed} != files:
            raise ValueError('manifest and native flash layout differ')
        app = manifest['application']
        if (app['path'] != flasher['app']['file'] or
                app['sha256'] != hashlib.sha256(verified[app['path']]).hexdigest()):
            raise ValueError('application identity mismatch')
        for role in ('bootloader', 'partition-table', 'app'):
            entry = flasher[role]
            if files.get(entry['offset']) != entry['file']:
                raise ValueError(f'missing flash input: {role}')
        expected_boot = 0 if chip == 'esp32s3' else 0x2000
        if int(flasher['bootloader']['offset'], 0) != expected_boot:
            raise ValueError('wrong bootloader offset for target')
        size = re.fullmatch(r'(\d+)MB', flasher['flash_settings']['flash_size'])
        if not size or not 1 <= int(size[1]) <= 128:
            raise ValueError('unsupported Flash size')
        capacity = int(size[1]) * 1024 * 1024
        parts = sorted((int(offset, 0), name, verified[name]) for offset, name in files.items())
        end = 0
        for offset, name, data in parts:
            if not data or offset < end or offset % 0x1000 or offset + len(data) > capacity:
                raise ValueError(f'overlapping, unaligned or out-of-range flash region: {name}')
            # Flash erases whole sectors; a second image must not share the last sector.
            end = (offset + len(data) + 0xfff) & ~0xfff
    return stem + '-merged.bin', chip, parts


def verify_merged(package: Path, image: Path) -> dict:
    """Compare every input byte and every erased gap, with flash base fixed at 0."""
    name, _, parts = merged_inputs(package)
    if image.name != name:
        raise ValueError('merged name differs from firmware identity')
    data = image.read_bytes()
    if len(data) != parts[-1][0] + len(parts[-1][2]):
        raise ValueError('merged image length differs from flash layout')
    end = 0
    for offset, relative, payload in parts:
        if data[end:offset] != b'\xff' * (offset - end):
            raise ValueError('merged padding must be erased bytes')
        if data[offset:offset + len(payload)] != payload:
            raise ValueError(f'merged image differs from input: {relative}')
        end = offset + len(payload)
    return {'path': name, 'size_bytes': len(data),
            'sha256': hashlib.sha256(data).hexdigest(),
            'flash_offset': '0x0', 'overwrites_gaps': True,
            'verification_status': 'BUILD_VERIFIED_HIL_PENDING'}


def merge_package(package: Path, output_dir: Path | None = None) -> Path:
    name, chip, parts = merged_inputs(package)
    output_dir = output_dir or package.parent
    output_dir.mkdir(parents=True, exist_ok=True)
    # Generate and verify in isolation; a failed command never publishes a partial BIN.
    with tempfile.TemporaryDirectory(prefix='.esp-merge-', dir=output_dir) as temporary:
        work = Path(temporary)
        candidate = work / name
        command = [sys.executable, '-m', 'esptool', '--chip', chip,
                   'merge_bin', '--format', 'raw', '--target-offset', '0',
                   '--output', str(candidate.resolve())]
        for offset, relative, data in parts:
            source = work / 'inputs' / relative
            source.parent.mkdir(parents=True, exist_ok=True)
            source.write_bytes(data)
            command.extend([hex(offset), str(source.resolve())])
        # Default keep options preserve native image headers and their digests.
        subprocess.run(command, check=True, capture_output=True, text=True)
        verify_merged(package, candidate)
        destination = output_dir / name
        if destination.exists() and destination.read_bytes() != candidate.read_bytes():
            raise ValueError('refusing to overwrite a different merged image with the same identity')
        candidate.replace(destination)
    return destination


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--package', type=Path, required=True)
    parser.add_argument('--output', type=Path, help='export directory; default: beside the source ZIP')
    args = parser.parse_args()
    try:
        output = args.output or args.package.parent
        image = merge_package(args.package, output)
        copy = output / args.package.name
        if copy.resolve() != args.package.resolve():
            if copy.exists() and copy.read_bytes() != args.package.read_bytes():
                raise ValueError('export ZIP already exists with different content')
            shutil.copyfile(args.package, copy)
        sums = ''.join(f'{hashlib.sha256(p.read_bytes()).hexdigest()}  {p.name}\n'
                       for p in (copy, image))
        (output / 'SHA256SUMS').write_text(sums, encoding='utf-8')
        print(json.dumps(verify_merged(copy, image), indent=2))
        print('Flash merged BIN at 0x0. Gaps, including NVS configuration, will be overwritten.')
        return 0
    except (OSError, ValueError, KeyError, zipfile.BadZipFile, subprocess.CalledProcessError) as exc:
        print(f'merge failed: {exc}', file=sys.stderr)
        if isinstance(exc, subprocess.CalledProcessError):
            print(exc.stderr or 'Activate the ESP-IDF environment providing esptool.', file=sys.stderr)
        return 1


if __name__ == '__main__':
    raise SystemExit(main())
