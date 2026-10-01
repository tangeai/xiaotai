#!/usr/bin/env python3
"""Reset the board and assert the offline-voice boot contract from UART logs."""

from __future__ import annotations

import argparse
import re
import sys
import time

import serial


ANSI_ESCAPE = re.compile(r"\x1b\[[0-?]*[ -/]*[@-~]")


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--port", required=True)
    parser.add_argument("--seconds", type=float, default=12.0)
    parser.add_argument("--version", required=True)
    args = parser.parse_args()

    uart = serial.Serial()
    uart.port = args.port
    uart.baudrate = 115200
    uart.timeout = 0.2
    # Match idf-monitor: set physical DTR/RTS low before open, return both
    # high, then pulse EN low. pySerial booleans are inverted by the board's
    # transistor/reset logic.
    uart.rts = True
    uart.dtr = True
    uart.open()
    try:
        uart.rts = False
        uart.dtr = False
        uart.reset_input_buffer()
        uart.rts = True
        time.sleep(0.12)
        uart.rts = False

        deadline = time.monotonic() + args.seconds
        chunks: list[bytes] = []
        while time.monotonic() < deadline:
            chunk = uart.read(4096)
            if chunk:
                chunks.append(chunk)
    finally:
        uart.close()

    log = ANSI_ESCAPE.sub("", b"".join(chunks).decode("utf-8", errors="replace"))
    failures: list[str] = []
    required = (
        f"App version:      {args.version}",
        "MultiNet prompt ready: model=mn7_cn",
        "MultiNet prompt detector armed after 4000 ms startup quiet period",
        "offline local wake and command recognition ready",
        "reserved 20480 internal bytes for later TiRTC bootstrap",
        "Wi-Fi connected, IP=",
        "released 20480-byte internal TiRTC bootstrap reserve",
        "SDK started",
    )
    forbidden = (
        "ESP_ERR_NO_MEM",
        "failed to create task",
        "offline local voice unavailable",
        "cannot create startup task",
        "could not reserve contiguous internal heap for TiRTC",
        "abort() was called",
        "Rebooting...",
        "heap_alloc: failed",
        "TiRTC start failed",
        "Guru Meditation Error",
        "stack overflow",
    )

    for marker in required:
        if marker not in log:
            failures.append(f"missing: {marker}")
    for marker in forbidden:
        if marker in log:
            failures.append(f"forbidden: {marker}")

    signal_lines = [
        line
        for line in log.splitlines()
        if any(
            marker in line
            for marker in (
                "App version:",
                "MultiNet prompt",
                "offline local",
                "reserved 20480",
                "Wi-Fi connected",
                "starter task internal stack remaining",
                "released 20480-byte",
                "TiRTC pre-init",
                "TiRTC post-init",
                "SDK started",
                "heap post-",
                "heap_alloc: failed",
                "TiRTC start failed",
                "Guru Meditation Error",
                "stack overflow",
                "ESP_ERR_NO_MEM",
                "failed to create task",
                "cannot create startup task",
                "could not reserve contiguous internal heap",
                "abort() was called",
                "Rebooting...",
            )
        )
    ]
    for line in signal_lines[-40:]:
        print(line)

    if failures:
        for failure in failures:
            print(f"FAIL: {failure}", file=sys.stderr)
        return 1

    print("PASS: MultiNet7 prompt detector, Wi-Fi and TiRTC reached ready state")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
