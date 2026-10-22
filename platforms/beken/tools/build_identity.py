#!/usr/bin/env python3
"""Create a boot-visible ID and bind it to the final BK7258 flash image."""

import hashlib
import json
import fcntl
import re
import shutil
import sys
from pathlib import Path


def write_json(path, value):
    path.write_text(json.dumps(value, indent=2, sort_keys=True) + "\n")


def next_build_number(counter_file):
    with counter_file.open('r+', encoding='utf-8') as counter:
        fcntl.flock(counter, fcntl.LOCK_EX)
        current = counter.read().strip()
        if re.fullmatch(r'[0-9]+', current) is None:
            raise ValueError('BUILD_NUMBER must contain one nonnegative integer')
        number = int(current) + 1
        counter.seek(0)
        counter.write(f'{number}\n')
        counter.truncate()
        counter.flush()
        return number


def prepare(directory, version, source_commit, counter_file,
            profile='development', purpose='Local development and debugging',
            board_id='lckfb-bk7258'):
    if not source_commit or any(c not in "0123456789abcdef" for c in source_commit):
        raise ValueError("source commit must be lowercase hexadecimal")
    if re.fullmatch(r'[0-9]+\.[0-9]+\.[0-9]+', version) is None:
        raise ValueError('firmware version must be major.minor.patch')
    if profile not in ('development', 'release'):
        raise ValueError('build profile must be development or release')
    if re.fullmatch(r"[a-z0-9]+(?:-[a-z0-9]+)*", board_id) is None:
        raise ValueError("invalid board ID")
    directory.mkdir(parents=True, exist_ok=True)
    number = next_build_number(counter_file)
    build_id = str(number)
    build_version = f'{version}+build.{number}'
    (directory / "xiaotai_build_identity.h").write_text(
        f'#define XIAOTAI_BUILD_ID "{build_id}"\n'
        f'#define XIAOTAI_BUILD_VERSION "{build_version}"\n')
    write_json(directory / "build_identity.json", {
        "board_id": board_id,
        "build_id": build_id,
        "build_number": number,
        "build_version": build_version,
        "version": version,
        "build_profile": profile,
        "purpose": purpose,
        "source_commit": source_commit,
        "all_app_sha256": None,
    })
    # The SDK reads APP_VERSION from the environment during CMake configure.
    # Ninja does not notice an environment-only change on the next build.
    # Keep objects, but force both native projects to configure the new value.
    for soc in ('bk7258_ap', 'bk7258'):
        (directory / soc / 'CMakeCache.txt').unlink(missing_ok=True)


def finalize(directory, artifact, ap_elf):
    path = directory / "build_identity.json"
    value = json.loads(path.read_text())
    header = (directory / "xiaotai_build_identity.h").read_text()
    if (f'"{value["build_id"]}"' not in header or
            f'"{value["build_version"]}"' not in header):
        raise ValueError("build identity header and metadata disagree")
    elf_bytes = ap_elf.read_bytes()
    if value["build_version"].encode() not in elf_bytes:
        raise ValueError("AP ELF does not contain this build ID")
    versions = set(re.findall(rb'[0-9]+\.[0-9]+\.[0-9]+\+build\.[0-9]+', elf_bytes))
    if versions != {value['build_version'].encode()}:
        raise ValueError(f"AP ELF contains mixed build versions: {sorted(versions)}")
    image = artifact.read_bytes()
    package = json.loads((directory / 'partitions/bk_package.json').read_text())
    # Beken inserts two CRC bytes after each 32 payload bytes. Compare the
    # actual flash payload, rather than trusting ELF strings or file names.
    binaries = {'app1.bin': ap_elf.with_suffix('.bin')}
    cp_bin = directory / 'bk7258/app.bin'
    if cp_bin.exists():
        binaries['app.bin'] = cp_bin
    for name, binary in binaries.items():
        part = next(p for p in package['section'] if p['firmware'] == name)
        offset = int(part['start_addr'], 0)
        expected = binary.read_bytes()
        if package['crc_enable']:
            actual = b''.join(image[i:i + 32] for i in
                              range(offset, offset + ((len(expected) + 31) // 32) * 34, 34))
        else:
            actual = image[offset:offset + len(expected)]
        label = 'AP' if name == 'app1.bin' else 'CP'
        if actual[:len(expected)] != expected:
            raise ValueError(f"packaged {label} payload differs from the linked binary")
    digest = hashlib.sha256(image).hexdigest()
    named = artifact.with_name(
        f"{value['board_id']}-{value['build_version']}-all-app.bin")
    if named.exists():
        if hashlib.sha256(named.read_bytes()).hexdigest() != digest:
            raise ValueError("versioned flash image already exists with different contents")
    else:
        shutil.copyfile(artifact, named)
    value["all_app_sha256"] = digest
    value["flash_file"] = named.name
    write_json(path, value)
    (artifact.parent / 'README.md').write_text(
        '# BK7258 firmware\n\n'
        f'Version: `{value["build_version"]}`\n\n'
        f'Profile: `{value["build_profile"]}`\n\n'
        f'Purpose: {value["purpose"]}\n\n'
        f'Flash file: [{named.name}]({named.name}) at `0x00000000`.\n\n'
        f'SHA-256: `{digest}`\n\n'
        f'SDK native image: `{artifact.resolve()}`. The named file is an identical copy.\n\n'
        'Build verified; hardware validation is pending for this exact image.\n',
        encoding='utf-8')
    print(named)


if __name__ == "__main__":
    if len(sys.argv) in (6, 8, 9) and sys.argv[1] == "prepare":
        prepare(Path(sys.argv[2]), sys.argv[3], sys.argv[4], Path(sys.argv[5]),
                *sys.argv[6:])
    elif len(sys.argv) == 5 and sys.argv[1] == "finalize":
        finalize(Path(sys.argv[2]), Path(sys.argv[3]), Path(sys.argv[4]))
    else:
        raise SystemExit("usage: build_identity.py prepare DIR VERSION COMMIT BUILD_NUMBER [PROFILE PURPOSE [BOARD_ID]] | finalize DIR IMAGE AP_ELF")
