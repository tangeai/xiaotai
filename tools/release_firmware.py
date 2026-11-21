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
from merge_esp_firmware import verify_merged

BOARDS = {
    'm5stack-cores3-lite': ('M5Stack CoreS3-Lite', 'm5stack-cores3-lite'),
    'lckfb-bk7258': ('立创·实战派 BK7258', 'bk7258'),
    'mgkfb-bk7258': ('觅感 AI 玩具开发板 BK7258', 'mgkfb-bk7258'),
    'lckfb-esp32s3': ('立创·实战派 ESP32-S3', 'lckfb-esp32s3'),
    'waveshare-esp32p4-touch-lcd-43c-v10': ('微雪 ESP32-P4-WIFI6-Touch-LCD-4.3', 'waveshare-esp32p4-touch-lcd-43c-v10'),
}


RELEASE_NOTE_SECTIONS = {
    '中文': ('版本概览', '新增', '调整', '修复', '已知问题与遗留事项', '验证情况', '下载与升级'),
    'English': ('Overview', 'Added', 'Changed', 'Fixed',
                'Known issues and outstanding work', 'Validation', 'Downloads and upgrade'),
}


def validate_release_notes(body: str) -> None:
    """Validate structure, not the factual equivalence of translations."""
    if re.search(r'\{\{.*?\}\}', body, flags=re.S):
        raise ValueError('release notes contain an unfilled placeholder')
    body = re.sub(r'<!--.*?-->', '', body, flags=re.S)
    languages = re.split(r'^## (.+?)\s*$', body, flags=re.M)
    if languages[1::2] != list(RELEASE_NOTE_SECTIONS):
        raise ValueError('release notes require Chinese followed by English')
    for language, content in zip(languages[1::2], languages[2::2]):
        sections = re.split(r'^### (.+?)\s*$', content, flags=re.M)
        if sections[1::2] != list(RELEASE_NOTE_SECTIONS[language]):
            raise ValueError(f'release notes have missing or misordered {language} sections')
        for title, text in zip(sections[1::2], sections[2::2]):
            if not re.sub(r'[\s*#>|`_-]', '', text):
                raise ValueError(f'release notes section is empty: {title}')


def release_plan(package: Path, merged: Path | None = None) -> dict:
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
    plan = {
        'board_id': board, 'product_version': product, 'build_number': number,
        'firmware_version': version, 'tag': f'{prefix}-v{product}',
        'title': f'{label} v{product} · build.{number}',
        'asset': package.name, 'package': str(package.resolve()),
        'sha256': hashlib.sha256(package.read_bytes()).hexdigest(),
        'application': application,
        'verification_status': manifest['verification_status'],
    }
    if merged is not None:
        plan['merged'] = verify_merged(package, merged)
    return plan


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--package', required=True, type=Path)
    parser.add_argument('--merged', type=Path, help='verify a sibling ESP merged BIN against the ZIP')
    parser.add_argument('--notes-file', type=Path, help='validate bilingual release notes; required before publication')
    args = parser.parse_args()
    try:
        if args.notes_file is not None:
            validate_release_notes(args.notes_file.read_text(encoding='utf-8'))
        plan = release_plan(args.package, args.merged)
        plan['notes_validated'] = args.notes_file is not None
        print(json.dumps(plan, ensure_ascii=False, indent=2))
        return 0
    except (OSError, ValueError, KeyError, zipfile.BadZipFile) as exc:
        print(f'release validation failed: {exc}', file=sys.stderr)
        return 1


if __name__ == '__main__':
    raise SystemExit(main())
