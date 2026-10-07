#!/usr/bin/env python3
"""Replay current and older compact firmware telemetry through the analyzer."""
from pathlib import Path
import runpy
import tempfile

parser = runpy.run_path(str(Path(__file__).with_name("analyze_media_performance.py")))
line = ("I (95143) call_video: VRX 640x480 in=5.0/672k q/d/c/o=5.0/4.9/4.5/4.5 "
        "drop=0/4 fail=0/0 age=154/345ms depth=1/1 buf=0/2@200ms adapt=0 "
        "ms=au/cvt/ppa:200/196/36 kd=233/193 gap=304/300 old=0/0 reset=0/0 ovf=0 "
        "parts=pack/swap/ui:138/21/0 max=au/cvt/ppa:316/349/190\n")
with tempfile.TemporaryDirectory(prefix="media-log-") as tmp:
    p = Path(tmp)/"serial.log"
    for variant in (line, line.replace("adapt=0 ", ""), line.replace("adapt=0", "adapt=1")):
        p.write_text(variant)
        result = parser["parse_log"](p)
        assert len(result.downlink) == 1, "compact VRX silently omitted"
        row = result.downlink[0]
        assert row["pack_ms"] == 138 and row["convert_ms"] == 196
        assert row["queue_age_avg_ms"] == 154 and row["queue_age_max_ms"] == 345
        assert row["presented_fps"] == 4.5 and row["display_drops"] == 4
print("PASS: performance analyzer accepts old/new compact logs without losing VRX samples")
