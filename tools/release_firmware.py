#!/usr/bin/env python3
"""Validate a firmware package and print canonical GitHub release metadata.

Read-only: this command never pushes, creates tags, uploads or replaces assets.
"""
import argparse
import hashlib
import json
from pathlib import Path
import re
import sys
import zipfile

from firmware_version import FirmwareVersion

BOARDS = {
    'lckfb-bk7258': ('BK7258', 'bk7258'),
    'lckfb-esp32s3': ('ESP32-S3', 'esp32s3'),
    'waveshare-esp32p4-touch-lcd-43c-v10': ('ESP32-P4', 'esp32p4'),
}


def release_plan(package: Path) -> dict:
    with zipfile.ZipFile(package) as archive:
        if len(archive.namelist()) != len(set(archive.namelist())):
            raise ValueError('duplicate archive entries')
        manifest = json.loads(archive.read('MANIFEST.json'))
        board = manifest['board_id']
        label, prefix = BOARDS[board]
        version = manifest['project_version']
        match = re.fullmatch(r'(.+)\+build\.([1-9][0-9]*)', version)
        if not match:
            raise ValueError('release requires an actual positive build number')
        product = str(FirmwareVersion.parse(match[1]))
        number = int(match[2])
        if package.name != f'{board}-{version}-validation.zip':
            raise ValueError('asset name does not match packaged identity')
        if manifest['verification_status'] not in ('BUILD_VERIFIED_HIL_PENDING', 'HIL_VERIFIED'):
            raise ValueError('unsupported verification status')
        records = manifest['files']
        if not records or len({r['path'] for r in records}) != len(records):
            raise ValueError('missing or duplicate file records')
        for record in records:
            payload = archive.read(record['path'])
            if (len(payload) != record['size_bytes'] or
                    hashlib.sha256(payload).hexdigest() != record['sha256']):
                raise ValueError(f"artifact checksum mismatch: {record['path']}")
        application = manifest['application']
        if not any(r['path'] == application['path'] and
                   r['sha256'] == application['sha256'] for r in records):
            raise ValueError('application missing from verified files')
    return {
        'board_id': board, 'product_version': product, 'build_number': number,
        'firmware_version': version, 'tag': f'{prefix}-v{product}',
        'title': f'{label} v{product} · build.{number}',
        'asset': package.name, 'package': str(package.resolve()),
        'sha256': hashlib.sha256(package.read_bytes()).hexdigest(),
        'application': application,
        'verification_status': manifest['verification_status'],
    }


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--package', required=True, type=Path)
    args = parser.parse_args()
    try:
        print(json.dumps(release_plan(args.package), ensure_ascii=False, indent=2))
        return 0
    except (OSError, ValueError, KeyError, zipfile.BadZipFile) as exc:
        print(f'release validation failed: {exc}', file=sys.stderr)
        return 1


if __name__ == '__main__':
    raise SystemExit(main())
