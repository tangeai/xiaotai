#!/usr/bin/env python3
"""Reject mixed-variable or stale sdkconfigs in the CoreS3 cache-line A/B test.

This checks resolved build inputs, not boot success or performance. Run after
idf.py configuration because sdkconfig.defaults does not override retained values.
"""
import argparse
from pathlib import Path
import re
import sys

LINE_KEYS = {'CONFIG_ESP32S3_DATA_CACHE_LINE_32B',
             'CONFIG_ESP32S3_DATA_CACHE_LINE_64B',
             'CONFIG_ESP32S3_DATA_CACHE_LINE_SIZE'}


def read_config(path):
    result = {}
    for line in Path(path).read_text().splitlines():
        match = re.fullmatch(r'(CONFIG_\w+)=(.*)', line)
        disabled = re.fullmatch(r'# (CONFIG_\w+) is not set', line)
        if match or disabled:
            key, value = (match[1], match[2]) if match else (disabled[1], 'n')
            if key in result:
                raise ValueError(f'duplicate config key: {key}')
            result[key] = value
    if not result:
        raise ValueError('empty sdkconfig')
    return result


def require_cache(values, line_bytes, label, resolved=True):
    if values.get('CONFIG_IDF_TARGET') != '"esp32s3"' or values.get('CONFIG_SPIRAM_MODE_QUAD') != 'y':
        raise ValueError(f'{label}: expected S3 Quad PSRAM configuration')
    if (values.get('CONFIG_SPIRAM_MODE_OCT', 'n') != 'n' or
            values.get('CONFIG_ESP32S3_DATA_CACHE_32KB') != 'y' or
            values.get('CONFIG_ESP32S3_DATA_CACHE_64KB', 'n') != 'n' or
            values.get('CONFIG_ESP32S3_DATA_CACHE_16KB', 'n') != 'n'):
        raise ValueError(f'{label}: must retain Quad PSRAM and 32 KB cache capacity')
    for size in (16, 32, 64):
        key = f'CONFIG_ESP32S3_DATA_CACHE_LINE_{size}B'
        if values.get(key, 'n') != ('y' if size == line_bytes else 'n'):
            raise ValueError(f'{label} cache line must be {line_bytes} B')
    if resolved:
        if int(values.get('CONFIG_ESP32S3_DATA_CACHE_SIZE', '0'), 0) != 32768:
            raise ValueError(f'{label}: resolved cache capacity must be 32768')
        if values.get('CONFIG_ESP32S3_DATA_CACHE_LINE_SIZE') != str(line_bytes):
            raise ValueError(f'{label} cache line derived value disagrees with selection')


def compare(baseline, candidate):
    require_cache(baseline, 32, 'baseline')
    require_cache(candidate, 64, 'candidate')
    changed = {k for k in baseline.keys() | candidate.keys()
               if baseline.get(k, 'n') != candidate.get(k, 'n')}
    if changed != LINE_KEYS:
        raise ValueError('unexpected config changes: ' + ', '.join(sorted(changed ^ LINE_KEYS)))
    return sorted(changed)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--baseline', type=Path, required=True)
    parser.add_argument('--candidate', type=Path, required=True)
    args = parser.parse_args()
    try:
        changed = compare(read_config(args.baseline), read_config(args.candidate))
    except (OSError, ValueError) as error:
        print(f'cache experiment rejected: {error}', file=sys.stderr)
        return 1
    print('PASS: cache line 32 B -> 64 B; capacity remains 32 KB; all other sdkconfig values unchanged')
    print('\n'.join(changed))
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
