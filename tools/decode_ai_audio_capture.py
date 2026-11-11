#!/usr/bin/env python3
"""Validate a stopped audio-capture dump and export private WAV/packet evidence.

Input is a serial log containing AUDIO_CAPTURE_DATA/END lines. Output files
contain user audio; use tests/.local/ or another untracked diagnostic directory.
WAVs concatenate captured frames; inter-reply silence is NOT reconstructed.
"""
import argparse
import ctypes
import ctypes.util
import json
import hashlib
from pathlib import Path
import re
import struct
import wave

HEADER = struct.Struct('<12I')


def parse_dump(text):
    data = bytearray()
    ended = False
    for line in text.splitlines():
        match = re.fullmatch(r'AUDIO_CAPTURE_DATA (\d+) ([0-9a-f]+)', line.strip())
        if match:
            if ended or int(match[1]) != len(data):
                raise ValueError('non-contiguous or duplicate capture chunks')
            chunk = bytes.fromhex(match[2])
            if len(data) + len(chunk) > 256 * 1024:
                raise ValueError('capture exceeds the device capacity')
            data.extend(chunk)
        elif line.startswith('AUDIO_CAPTURE_END '):
            match = re.fullmatch(r'AUDIO_CAPTURE_END bytes=(\d+)', line.strip())
            if ended or not match or int(match[1]) != len(data):
                raise ValueError('capture end length mismatch')
            ended = True
    if not ended or not data:
        raise ValueError('empty or incomplete capture; stop and dump again')
    records = []
    offset = 0
    while offset < len(data):
        if len(data) - offset < HEADER.size:
            raise ValueError('truncated record header')
        magic, gen, ts, rate, channels, bits, consumed, raw_size, pcm_size, begin, end, played = HEADER.unpack_from(data, offset)
        offset += HEADER.size
        if magic != 0x31445541 or not raw_size or not pcm_size or raw_size + pcm_size > len(data) - offset:
            raise ValueError('invalid or truncated audio record')
        if (rate, channels, bits) != (16000, 1, 16) or pcm_size % 2:
            raise ValueError(f'unexpected decoder format: {rate}/{channels}/{bits}, PCM bytes={pcm_size}')
        if records and gen != records[0]['generation']:
            raise ValueError('capture mixes decoder generations')
        records.append(dict(generation=gen, timestamp_ms=ts, sample_rate=rate,
            channels=channels, bits=bits, consumed=consumed, decode_ms=begin,
            write_ms=end, played=bool(played), opus=bytes(data[offset:offset+raw_size]),
            pcm=bytes(data[offset+raw_size:offset+raw_size+pcm_size])))
        offset += raw_size + pcm_size
    return records


def decode_opus(records):
    name = ctypes.util.find_library('opus')
    if not name:
        raise RuntimeError('libopus is required for independent host decoding')
    lib = ctypes.CDLL(name)
    lib.opus_decoder_create.argtypes = [ctypes.c_int32, ctypes.c_int, ctypes.POINTER(ctypes.c_int)]
    lib.opus_decoder_create.restype = ctypes.c_void_p
    lib.opus_decode.argtypes = [ctypes.c_void_p, ctypes.c_char_p, ctypes.c_int32,
        ctypes.POINTER(ctypes.c_int16), ctypes.c_int, ctypes.c_int]
    lib.opus_decode.restype = ctypes.c_int
    lib.opus_decoder_destroy.argtypes = [ctypes.c_void_p]
    error = ctypes.c_int()
    decoder = lib.opus_decoder_create(16000, 1, ctypes.byref(error))
    if not decoder or error.value:
        raise RuntimeError(f'opus decoder open failed: {error.value}')
    result = []
    try:
        for record in records:
            output = (ctypes.c_int16 * 1920)()  # Maximum 120 ms at 16 kHz.
            packet = record['opus']
            count = lib.opus_decode(decoder, packet, len(packet), output, 1920, 0)
            if count < 0:
                raise ValueError(f'opus decode failed at packet {len(result)}: {count}')
            result.append(struct.pack('<' + 'h' * count, *output[:count]))
    finally:
        lib.opus_decoder_destroy(decoder)
    return result


def write_wav(path, frames):
    with wave.open(str(path), 'wb') as out:
        out.setparams((1, 2, 16000, 0, 'NONE', 'not compressed'))
        for frame in frames:
            out.writeframesraw(frame)


def export_capture(records, output, codec='opus'):
    """Preserve exact payloads and compare independent decode, without overwriting."""
    host = decode_opus(records) if codec == 'opus' else [r['opus'] for r in records]
    if codec == 'pcm' and any(len(frame) % 2 for frame in host):
        raise ValueError('PCM input contains an incomplete 16-bit sample')
    output.mkdir(parents=True, exist_ok=False)
    raw_dir = output / 'raw-packets'
    raw_dir.mkdir()
    write_wav(output / 'device.wav', [r['pcm'] for r in records])
    write_wav(output / ('host-opus.wav' if codec == 'opus' else 'received-pcm.wav'), host)
    metadata = []
    for index, record in enumerate(records):
        raw_path = f'raw-packets/{index:06d}.bin'
        (output / raw_path).write_bytes(record['opus'])
        row = {k: v for k, v in record.items() if k not in ('pcm', 'opus')}
        row[codec + '_hex'] = record['opus'].hex()
        row.update(index=index, codec=codec, raw_path=raw_path,
            sha256=hashlib.sha256(record['opus']).hexdigest(),
            device_samples=len(record['pcm'])//2, host_samples=len(host[index])//2)
        metadata.append(row)
    (output / 'packets.json').write_text(json.dumps(metadata, indent=2) + '\n')
    report = dict(frames=len(records),
        device_samples=sum(len(r['pcm'])//2 for r in records),
        host_samples=sum(len(frame)//2 for frame in host),
        partial_input_frames=sum(r['consumed'] != len(r['opus']) for r in records),
        unwritten_frames=sum(not r['played'] for r in records),
        duration_mismatch_frames=sum(len(r['pcm']) != len(frame) for r, frame in zip(records, host)),
        pcm_identical=all(r['pcm'] == frame for r, frame in zip(records, host)),
        limits='Payloads captured in playback worker; compare with callback trace separately. '
               'WAV concatenates frames without timestamp gaps; not a speaker recording. '
               'A valid dump does not prove the capture covered the whole reply; inspect stop reason.')
    (output / 'comparison.json').write_text(json.dumps(report, indent=2) + '\n')
    return report


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('log', type=Path)
    parser.add_argument('--output', required=True, type=Path)
    parser.add_argument('--codec', choices=('opus', 'pcm'), default='opus')
    args = parser.parse_args()
    records = parse_dump(args.log.read_text())
    print(json.dumps(export_capture(records, args.output, args.codec), indent=2))


if __name__ == '__main__':
    main()
