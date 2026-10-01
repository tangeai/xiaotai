#!/usr/bin/env python3
"""Classify the ESP32 call downlink while a human keeps a call active.

Usage: tools/hitl_downlink_audio_status.py [/dev/ttyACM0] [seconds]

The script deliberately uses the firmware's public ``status`` console command
instead of poking media state.  During the sampling period the remote peer must
continuously speak; its verdict is a tight, repeatable boundary check for the
otherwise human-triggered symptom "the remote peer is not audible".
"""

from __future__ import annotations

import re
import sys
import time

import serial


PORT = sys.argv[1] if len(sys.argv) > 1 else "/dev/ttyACM0"
SECONDS = float(sys.argv[2]) if len(sys.argv) > 2 else 12.0
MEDIA_RE = re.compile(r"Media: .*audio-rx=(\d+) dropped=(\d+)")
DOWNLINK_RE = re.compile(
    r"Downlink: decoded=(\d+) played=(\d+) decode-fail=(\d+) gate=(\d+) i2s-fail=(\d+)"
)


def verdict(media: tuple[int, int] | None,
            downlink: tuple[int, int, int, int, int] | None) -> tuple[int, str]:
    if media is None or downlink is None:
        return 3, "INCONCLUSIVE: status response was incomplete"
    received, _dropped = media
    decoded, played, decode_failed, gated, write_failed = downlink
    if received == 0:
        return 2, "RED no downlink frames reached starter_media"
    if decoded == 0 or decode_failed > 0:
        return 2, "RED downlink decode failed"
    if played == 0 and (gated > 0 or write_failed > 0):
        return 2, "RED decoded audio was blocked before speaker output"
    if played == 0:
        return 2, "RED decoded audio never reached I2S"
    return 0, "GREEN frames decoded and written to I2S; inspect codec/slot routing next"


def main() -> int:
    latest_media: tuple[int, int] | None = None
    latest_downlink: tuple[int, int, int, int, int] | None = None
    deadline = time.monotonic() + SECONDS
    next_status = 0.0

    with serial.Serial(PORT, 115200, timeout=0.15) as uart:
        uart.reset_input_buffer()
        print(f"HIL_DOWNLINK_READY port={PORT} seconds={SECONDS:g}", flush=True)
        while time.monotonic() < deadline:
            now = time.monotonic()
            if now >= next_status:
                uart.write(b"status\r\n")
                uart.flush()
                next_status = now + 1.0
            line = uart.readline().decode("utf-8", errors="replace").strip()
            media_match = MEDIA_RE.search(line)
            if media_match:
                latest_media = tuple(map(int, media_match.groups()))
                print(line, flush=True)
            downlink_match = DOWNLINK_RE.search(line)
            if downlink_match:
                latest_downlink = tuple(map(int, downlink_match.groups()))
                print(line, flush=True)

    code, message = verdict(latest_media, latest_downlink)
    print(f"HIL_DOWNLINK_{'PASS' if code == 0 else 'FAIL'} {message}", flush=True)
    return code


if __name__ == "__main__":
    raise SystemExit(main())
