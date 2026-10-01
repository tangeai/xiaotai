#!/usr/bin/env python3
import re
import sys
import time

import serial

port = sys.argv[1] if len(sys.argv) > 1 else "/dev/ttyACM0"
seconds = float(sys.argv[2]) if len(sys.argv) > 2 else 300.0
reset_on_open = "--reset" in sys.argv[3:]
bad = re.compile(
    r"Guru|assert failed|abort|Rebooting|stack overflow|Task watchdog|WDT|"
    r"ESP_ERR_NO_MEM|heap_alloc: failed|failed to create|AI connection submission failed",
    re.I,
)
interesting = (
    "Recognizer:", "MultiNet prompt", "custom wake phrase", "wake-window command",
    "full local phrase", "voice intent handled", "speaker volume=",
    "state=ai-", "external connection ready", "SDK started",
    "Wi-Fi connected", "SNTP", "NTP", "TiRTC", "VoIP", "profile",
    "MQTT connected", "connection ended", "media", "audio", "state=",
    "WHIP", "stack overflow",
    "call", "CALL", "incoming", "connection mode", "HTTP",
)

with serial.Serial(port, 115200, timeout=0.2) as uart:
    if reset_on_open:
        uart.dtr = False
        uart.rts = True
        time.sleep(0.05)
        uart.rts = False
        time.sleep(0.05)
        uart.dtr = True
    uart.reset_input_buffer()
    deadline = time.monotonic() + seconds
    print("MONITOR_READY", flush=True)
    while time.monotonic() < deadline:
        line = uart.readline().decode("utf-8", errors="replace").strip()
        if line and (bad.search(line) or any(mark in line for mark in interesting)):
            print(line, flush=True)
print("MONITOR_DONE", flush=True)
