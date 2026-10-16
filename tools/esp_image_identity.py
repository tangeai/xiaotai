"""Validate the ESP-IDF application image and its matching symbol file.

Layout follows ESP-IDF esp_image_header_t/esp_app_desc_t and esptool's
segment checksum and appended SHA-256 format. These checks are offline.
"""
from __future__ import annotations

import hashlib
from pathlib import Path
import re
import struct

from firmware_version import esp_build_version

CHIP_IDS = {'esp32s3': 9, 'esp32p4': 18}


def validate_application(project: Path, app: Path, elf: Path,
                         description: dict, chip: str) -> dict:
    match = re.search(r'set\(PROJECT_VER\s+"([^"]+)"\)',
                      (project / 'CMakeLists.txt').read_text())
    if match is None:
        raise ValueError('missing project firmware version')
    version = esp_build_version(project, match.group(1))
    if version != description['project_version']:
        raise ValueError('stale build: project and build versions differ')
    if chip not in CHIP_IDS or chip != description.get('target'):
        raise ValueError('unsupported or mismatched image target')
    sdkconfig = project / 'sdkconfig'
    if sdkconfig.exists():
        target = re.search(r'^CONFIG_IDF_TARGET="([^"]+)"$',
                           sdkconfig.read_text(), re.MULTILINE)
        if target is None or target.group(1) != chip:
            raise ValueError('image target differs from project configuration')
    data = app.read_bytes()
    symbols = elf.read_bytes()
    if not symbols.startswith(b'\x7fELF'):
        raise ValueError('invalid ELF symbol file')
    if (len(data) < 32 or data[0] != 0xe9 or not 1 <= data[1] <= 16 or
            struct.unpack_from('<H', data, 12)[0] != CHIP_IDS[chip] or
            data[23] not in (0, 1)):
        raise ValueError('invalid ESP application image header or chip')
    offset, checksum = 24, 0xef
    descriptor = None
    for index in range(data[1]):
        if offset + 8 > len(data):
            raise ValueError('truncated ESP segment header')
        length = struct.unpack_from('<I', data, offset + 4)[0]
        offset += 8
        if offset + length > len(data):
            raise ValueError('truncated ESP segment')
        segment = data[offset:offset + length]
        if index == 0:
            if length < 256 or struct.unpack_from('<I', segment)[0] != 0xabcd5432:
                raise ValueError('missing ESP application descriptor')
            descriptor = segment[:256]
        for byte in segment:
            checksum ^= byte
        offset += length
    checksum_offset = offset + (15 - offset % 16)
    if checksum_offset >= len(data) or data[checksum_offset] != checksum:
        raise ValueError('invalid ESP image checksum')
    end = checksum_offset + 1
    if data[23]:
        if data[end:end + 32] != hashlib.sha256(data[:end]).digest():
            raise ValueError('invalid ESP image SHA-256')
        end += 32
    # This packager supports the repository's unsigned native applications.
    if end != len(data):
        raise ValueError('unexpected image trailer; signed images need explicit validation')
    def string_at(offset: int) -> str:
        try:
            return descriptor[offset:offset + 32].split(b'\0', 1)[0].decode('utf-8')
        except UnicodeDecodeError as exc:
            raise ValueError('invalid application descriptor text') from exc
    if string_at(16) != version:
        raise ValueError('application descriptor version differs from project version')
    if string_at(48) != description.get('project_name'):
        raise ValueError('application descriptor project name differs from build')
    elf_digest = hashlib.sha256(symbols).digest()
    if descriptor[144:176] != elf_digest:
        raise ValueError('application and ELF SHA-256 do not match')
    return {'version': version, 'target': chip, 'elf_sha256': elf_digest.hex()}
