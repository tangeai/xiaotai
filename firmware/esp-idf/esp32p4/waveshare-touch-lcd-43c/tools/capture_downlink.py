#!/usr/bin/env python3
"""Capture one pre-render WeChat downlink JPEG over the P4 serial console."""
import argparse
import re
import time
from pathlib import Path

from capture_uplink import Terminal, firmware_identity, fnv


class DownlinkDump:
    def __init__(self):
        self.data = bytearray()
        self.meta = None

    def feed(self, line):
        begin = re.search(r"DCAP BEGIN bytes=(\d+) fnv=([0-9a-f]{8}) generation=(\d+)", line)
        if begin:
            if self.meta is not None:
                raise ValueError("duplicate downlink header")
            self.meta = (int(begin[1]), int(begin[2], 16), int(begin[3]))
            if not 4 <= self.meta[0] <= 256 * 1024:
                raise ValueError("invalid JPEG size")
        elif "DCAP DATA " in line:
            match = re.search(r"DCAP DATA ([0-9a-f]{8}) ([0-9a-f]+)\s*$", line)
            if self.meta is None or not match or int(match[1], 16) != len(self.data):
                raise ValueError("missing or out-of-order JPEG data")
            chunk = bytes.fromhex(match[2])
            if not 0 < len(chunk) <= 64 or len(self.data) + len(chunk) > self.meta[0]:
                raise ValueError("invalid JPEG chunk")
            self.data.extend(chunk)
        elif "DCAP END" in line:
            if (self.meta is None or len(self.data) != self.meta[0]
                    or fnv(self.data) != self.meta[1]
                    or self.data[:2] != b"\xff\xd8"
                    or self.data[-2:] != b"\xff\xd9"):
                raise ValueError("incomplete or corrupted JPEG")
            return True
        elif "DCAP ERROR" in line:
            raise RuntimeError(line[line.index("DCAP ERROR"):].strip())
        return False


def until_downlink(terminal, marker, timeout=15):
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        line = terminal.readline().decode("ascii", errors="replace")
        if "DCAP ERROR" in line:
            raise RuntimeError(line[line.index("DCAP ERROR"):].strip())
        if marker in line:
            return line[line.index(marker):].strip()
    raise TimeoutError(f"did not receive {marker}; check port and call state")


def capture(port, output, dump_only=False):
    import serial

    with serial.Serial(port, 115200, timeout=0.1, write_timeout=5, exclusive=True) as raw:
        terminal = Terminal(raw)
        if not dump_only:
            input("建立微信视频通话并保持手机、标识纸位置不变；画面出现后按 Enter 开始抓一帧：")
        identity = firmware_identity(terminal)
        if not dump_only:
            terminal.command("downlink-capture status")
            previous = until_downlink(terminal, "DCAP STATUS")
            if not re.search(r"active=0 bytes=0 .*reason=empty", previous):
                raise RuntimeError("previous capture retained; use --dump-only or clear it explicitly")
            terminal.command("downlink-capture start")
            print(until_downlink(terminal, "DCAP ARMED"))
            deadline = time.monotonic() + 10
            while time.monotonic() < deadline:
                terminal.command("downlink-capture status")
                status = until_downlink(terminal, "DCAP STATUS", timeout=3)
                match = re.search(r"active=(\d+) bytes=(\d+) generation=(\d+) reason=(\S+)", status)
                if not match:
                    raise RuntimeError("downlink status is incomplete")
                if match[1] == "0":
                    if int(match[2]) == 0:
                        raise RuntimeError(f"no JPEG captured: {match[4]}")
                    break
            else:
                raise TimeoutError("no complete downlink JPEG within 10 seconds")

            input("现在挂断通话，确认设备已断开后按 Enter 导出：")
        terminal.command("downlink-capture dump")
        dump = DownlinkDump()
        deadline = time.monotonic() + 60
        while time.monotonic() < deadline:
            if dump.feed(terminal.readline().decode("ascii", errors="replace")):
                break
        else:
            raise TimeoutError("JPEG export timed out; retained on device")

        with output.open("xb") as file:
            file.write(dump.data)
        with output.with_suffix(".txt").open("x") as file:
            file.write(identity + "\n")
            file.write(f"bytes={dump.meta[0]} fnv={dump.meta[1]:08x} generation={dump.meta[2]}\n")
        terminal.command("downlink-capture clear")
        until_downlink(terminal, "DCAP STATUS")
    print(f"已保存并校验抓取帧：{output}")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--port", required=True)
    parser.add_argument("--output", required=True, type=Path)
    parser.add_argument("--dump-only", action="store_true", help="retry exporting a retained frame after hangup")
    args = parser.parse_args()
    if args.output.exists() or args.output.with_suffix(".txt").exists():
        parser.error("output or companion file exists; choose a new path")
    capture(args.port, args.output, args.dump_only)


if __name__ == "__main__":
    try:
        main()
    except (TimeoutError, RuntimeError, OSError, ValueError) as error:
        raise SystemExit(f"下行抓帧未完成：{error}") from None
