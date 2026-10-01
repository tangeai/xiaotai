#!/usr/bin/env python3
"""Start one AI session and assert that TiRTC reaches the external connection."""

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
    parser.add_argument("--seconds", type=float, default=20.0)
    args = parser.parse_args()

    deadline = time.monotonic() + args.seconds
    chunks: list[bytes] = []
    with serial.Serial(args.port, 115200, timeout=0.2) as uart:
        uart.reset_input_buffer()
        uart.write(b"\nai-start\n")
        uart.flush()
        while time.monotonic() < deadline:
            chunk = uart.read(4096)
            if chunk:
                chunks.append(chunk)

    log = ANSI_ESCAPE.sub("", b"".join(chunks).decode("utf-8", errors="replace"))
    signal_markers = (
        "state=ai-connecting",
        "realtime connect gate opened:",
        "external connect reserve released:",
        "heap_alloc: failed",
        "AI connection submission failed",
        "external connection ready mode=2",
        "AI start_session sent",
        "state=ai-active",
        "AI start_session was rejected",
        "unsupported audio profile",
        "session setup timed out",
        "stack overflow",
        "Rebooting...",
    )
    for line in log.splitlines():
        if any(marker in line for marker in signal_markers):
            print(line)

    failures: list[str] = []
    if "state=ai-connecting" not in log:
        failures.append("AI runtime did not enter ai-connecting")
    if "heap_alloc: failed" in log:
        failures.append("an allocation failed during AI connect")
    if "AI connection submission failed" in log:
        failures.append("TiRTC rejected the external-connect submission")
    if "stack overflow" in log or "Rebooting..." in log:
        failures.append("the AI session overflowed a task stack or rebooted")
    if "external connection ready mode=2" not in log:
        failures.append("TiRTC AI external connection did not become ready")
    if "AI start_session sent" not in log:
        failures.append("AI start_session was not sent")

    if failures:
        for failure in failures:
            print(f"FAIL: {failure}", file=sys.stderr)
        return 1

    print("PASS: TiRTC AI connection became ready and start_session was sent")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
