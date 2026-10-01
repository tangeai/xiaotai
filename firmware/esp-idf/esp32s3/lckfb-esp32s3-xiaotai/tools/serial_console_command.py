#!/usr/bin/env python3
"""Send one development-console command without resetting the target."""

from __future__ import annotations

import argparse
import sys
import time

import serial


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--port", required=True)
    parser.add_argument("--command", required=True)
    parser.add_argument("--seconds", type=float, default=4.0)
    args = parser.parse_args()

    deadline = time.monotonic() + args.seconds
    with serial.Serial(args.port, 115200, timeout=0.2) as uart:
        uart.write(("\n" + args.command + "\n").encode("utf-8"))
        uart.flush()
        while time.monotonic() < deadline:
            data = uart.read(4096)
            if data:
                sys.stdout.write(data.decode("utf-8", errors="replace"))
                sys.stdout.flush()
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
