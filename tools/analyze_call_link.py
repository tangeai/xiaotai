#!/usr/bin/env python3
"""Compare passive call evidence; device uptimes are never assumed synchronized."""
import argparse
import json
import re
from pathlib import Path


TIME = re.compile(r'[IWE] \((\d+)\)')
START = re.compile(r'media session started mode=(\d+) generation=(\d+)')
HOST = re.compile(r'host rx-valid/stack-ok/stack-fail=(\d+)/(\d+)/(\d+) '
                  r'tx-queued/drop/bus-written=(\d+)/(\d+)/(\d+) throttle=(\d+)')
VIDEO = re.compile(r'downlink generation=(\d+) mode=(\d+) tx=(\d+) tx-fail=(\d+) '
                   r'tx-ret=(-?\d+) send-buffer=(\d+)/(\d+) subscribed=\d+ rx=(\d+)')
CAMERA = re.compile(r'CAM \d+x\d+@(\d+) f=([\d.]+) br=(\d+)k')
RENDER = re.compile(r'VRX \d+x\d+ in=([\d.]+)/(\d+)k')
GAP = re.compile(r'video stall: stage=(\w+) decode=(\d+)us input_gap=(\d+)us')


def summarize(text):
    """Only export whitelisted numeric observations, never raw serial content."""
    calls = []
    call = None
    previous_host = previous_video = None
    for line in text.splitlines():
        timestamp = TIME.search(line)
        at = int(timestamp[1]) if timestamp else None
        # Task logs can be reordered; a backwards timestamp alone is not proof
        # of reboot. An explicit boot marker or the next media start separates
        # captures. Incomplete logs remain incomplete evidence.
        if ('starter_main: firmware version=' in line
                or 'cpu_start: Pro cpu start user code' in line):
            call = None
        start = START.search(line)
        if start and at is not None:
            call = {'mode': int(start[1]), 'generation': int(start[2]),
                    'start_ms': at, 'host_samples': [], 'host_intervals': [],
                    'video_samples': [], 'video_intervals': [], 'camera': [],
                    'renderer_input': [], 'input_gaps': [],
                    'audio_overflow_observations': [], 'counter_resets': 0}
            calls.append(call)
            previous_host = previous_video = None
            continue
        if 'runtime state 6 -> 0' in line:
            call = None
        if call is None:
            continue
        if at is not None and at < call['start_ms']:
            continue
        host = HOST.search(line)
        if host and at is not None:
            keys = ('rx-valid', 'stack-ok', 'stack-fail', 'tx-queued', 'tx-drop', 'bus-written')
            values = dict(zip(keys, map(int, host.groups()[:6])))
            row = {'at_ms': at, **values, 'throttle': int(host[7])}
            call['host_samples'].append(row)
            if previous_host is not None and at > previous_host['at_ms']:
                delta = {key: row[key] - previous_host[key] for key in keys}
                if all(value >= 0 for value in delta.values()):
                    call['host_intervals'].append({'from_ms': previous_host['at_ms'],
                                                  'to_ms': at, 'delta': delta})
                else:
                    # Reset/wrap cannot be distinguished from these logs alone.
                    call['counter_resets'] += 1
            previous_host = row
        video = VIDEO.search(line)
        if (video and at is not None and int(video[1]) == call['generation']
                and int(video[2]) == call['mode']):
            row = {'at_ms': at, 'tx': int(video[3]), 'tx_fail': int(video[4]),
                   'tx_ret': int(video[5]), 'send_buffer_bytes': int(video[6]),
                   'send_buffer_capacity': int(video[7]), 'rx': int(video[8])}
            call['video_samples'].append(row)
            if previous_video is not None and at > previous_video['at_ms']:
                elapsed = (at - previous_video['at_ms']) / 1000
                if row['tx'] >= previous_video['tx'] and row['rx'] >= previous_video['rx']:
                    call['video_intervals'].append({
                        'from_ms': previous_video['at_ms'], 'to_ms': at,
                        'sdk_tx_accepted_fps': (row['tx'] - previous_video['tx']) / elapsed,
                        'rx_fps': (row['rx'] - previous_video['rx']) / elapsed,
                        'send_buffer_delta_bytes': row['send_buffer_bytes'] - previous_video['send_buffer_bytes']})
                else:
                    call['counter_resets'] += 1
            previous_video = row
        camera = CAMERA.search(line)
        if camera and at is not None:
            call['camera'].append({'at_ms': at, 'target_fps': int(camera[1]),
                                   'fps': float(camera[2]), 'kbps': int(camera[3])})
        render = RENDER.search(line)
        if render and at is not None:
            call['renderer_input'].append({'at_ms': at, 'fps': float(render[1]), 'kbps': int(render[2])})
        gap = GAP.search(line)
        if gap and at is not None:
            call['input_gaps'].append({'at_ms': at, 'stage': gap[1],
                                      'decode_ms': int(gap[2]) / 1000,
                                      'input_gap_ms': int(gap[3]) / 1000})
        overflow = re.search(r'Audio RX buffer: slots=\d+ overflow=(\d+)', line)
        if overflow:
            call['audio_overflow_observations'].append(int(overflow[1]))
    return {'calls': calls}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('local_log', type=Path)
    parser.add_argument('--peer-log', type=Path)
    parser.add_argument('--out', type=Path)
    args = parser.parse_args()
    report = {'limitations': [
        'Pair calls and mark the audible stall manually; device uptimes are independent.',
        'Host counters cover all STA traffic, not just this call; samples are not an atomic group.',
        'SDK TX acceptance and SDIO bus writes do not prove remote delivery.',
        'Missing samples are unknown, not zero; this report does not assign root cause.'],
        'local': summarize(args.local_log.read_text(errors='replace'))}
    if args.peer_log:
        report['peer'] = summarize(args.peer_log.read_text(errors='replace'))
    output = json.dumps(report, ensure_ascii=False, indent=2) + '\n'
    if args.out:
        args.out.parent.mkdir(parents=True, exist_ok=True)
        args.out.write_text(output)
    else:
        print(output, end='')


if __name__ == '__main__':
    main()
