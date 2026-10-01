#!/usr/bin/env python3
"""Randomized real-browser STREAM connect/disconnect soak test.

Authentication is loaded from a Playwright storage-state file so credentials
never appear in arguments, logs, source, or the result JSONL.
"""

from __future__ import annotations

import argparse
import json
import random
import statistics
import sys
import time
from datetime import datetime, timezone
from pathlib import Path
from urllib.parse import quote


def dwell_seconds(rng: random.Random, scale: float) -> float:
    bucket = rng.random()
    if bucket < 0.70:
        return rng.uniform(5, 30) * scale
    if bucket < 0.95:
        return rng.uniform(60, 300) * scale
    return rng.uniform(600, 1800) * scale


def dwell_plan(value: str) -> list[float]:
    plan: list[float] = []
    try:
        for item in value.split(","):
            seconds, count = item.split(":", 1)
            plan.extend([float(seconds)] * int(count))
    except (ValueError, TypeError) as error:
        raise argparse.ArgumentTypeError(
            "use SECONDS:COUNT pairs, for example 60:60,300:30,600:10"
        ) from error
    if not plan or any(seconds <= 0 for seconds in plan):
        raise argparse.ArgumentTypeError("dwell plan values must be positive")
    return plan


def percentile(values: list[float], fraction: float) -> float | None:
    if not values:
        return None
    ordered = sorted(values)
    return ordered[min(len(ordered) - 1, int((len(ordered) - 1) * fraction))]


def utc_now() -> str:
    return datetime.now(timezone.utc).isoformat(timespec="milliseconds")


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--base-url", required=True)
    device = parser.add_mutually_exclusive_group(required=True)
    device.add_argument("--device-id")
    device.add_argument("--device-name")
    parser.add_argument("--storage-state", type=Path, required=True)
    parser.add_argument("--cycles", type=int, default=1000)
    parser.add_argument("--dwell-plan", type=dwell_plan,
                        help="Exact SECONDS:COUNT buckets; overrides --cycles")
    parser.add_argument("--seed", type=int, default=7258)
    parser.add_argument("--time-scale", type=float, default=1.0,
                        help="Use a value below 1 only for harness smoke tests")
    parser.add_argument("--connect-timeout", type=float, default=20.0)
    parser.add_argument("--abrupt-close-rate", type=float, default=0.10)
    parser.add_argument("--output", type=Path,
                        default=Path("output/bk7258-web-stability.jsonl"))
    parser.add_argument("--browser-executable", type=Path)
    args = parser.parse_args()
    if args.cycles <= 0 or args.time_scale <= 0:
        parser.error("cycles and time-scale must be positive")
    try:
        from playwright.sync_api import sync_playwright
    except ImportError:
        print("Install the test-only dependency: pip install playwright && "
              "playwright install chromium", file=sys.stderr)
        return 2

    rng = random.Random(args.seed)
    planned_dwells = args.dwell_plan or [
        dwell_seconds(rng, args.time_scale) for _ in range(args.cycles)
    ]
    if args.dwell_plan:
        planned_dwells = [seconds * args.time_scale for seconds in planned_dwells]
        rng.shuffle(planned_dwells)
    cycle_count = len(planned_dwells)
    args.output.parent.mkdir(parents=True, exist_ok=True)
    device_id = args.device_id
    records = []
    launch_options = {"headless": True}
    if args.browser_executable:
        launch_options["executable_path"] = str(args.browser_executable)
    with args.output.open("w", encoding="utf-8") as result, sync_playwright() as pw:
        browser = pw.chromium.launch(**launch_options)
        context = browser.new_context(storage_state=str(args.storage_state))
        if device_id is None:
            discovery = context.new_page()
            discovery.goto(args.base_url.rstrip("/") + "/devices",
                           wait_until="domcontentloaded",
                           timeout=int(args.connect_timeout * 1000))
            discovery.wait_for_selector("[data-card-device]",
                                        timeout=int(args.connect_timeout * 1000))
            device_id = discovery.evaluate(
                "name => { const cards=[...document.querySelectorAll('[data-card-device]')];"
                "const matches=cards.filter(card=>card.querySelector('h2')?.textContent.trim()===name);"
                "if(matches.length!==1) { const available=cards.map(card=>({"
                "name:card.querySelector('h2')?.textContent.trim()||'',"
                "online:card.querySelector('[data-online-status]')?.textContent.trim()||''}));"
                "throw new Error(`device ${name} matches ${matches.length}; available=${JSON.stringify(available)}`); }"
                "return matches[0].dataset.cardDevice; }",
                args.device_name,
            )
            discovery.close()
        player = (args.base_url.rstrip("/") + "/player?device_id=" +
                  quote(device_id, safe=""))
        for cycle, stay in enumerate(planned_dwells, start=1):
            page = context.new_page()
            started = time.monotonic()
            record = {
                "cycle": cycle,
                "seed": args.seed,
                "started_at": utc_now(),
                "planned_dwell_s": round(stay, 3),
                "abrupt": rng.random() < args.abrupt_close_rate,
                "phase": "navigate",
            }
            try:
                page.goto(player, wait_until="domcontentloaded",
                          timeout=int(args.connect_timeout * 1000))
                record["phase"] = "connect"
                page.wait_for_function(
                    "document.querySelector('#status-text-pc')?.textContent === '已连接'",
                    timeout=int(args.connect_timeout * 1000),
                )
                record["connect_ms"] = round((time.monotonic() - started) * 1000)
                first_frame_started = time.monotonic()
                record["phase"] = "first_frame"
                page.wait_for_function(
                    "(() => { const c=document.querySelector('#canvas'); "
                    # HTML's untouched canvas is 300x150.  The TiRTC video
                    # renderer resizes its backing store after a decoded frame.
                    "return c && c.width >= 320 && c.height >= 240; })()",
                    timeout=int(args.connect_timeout * 1000),
                )
                record["first_frame_ms"] = round(
                    (time.monotonic() - first_frame_started) * 1000
                )
                watch_started = time.monotonic()
                samples = 0
                record["phase"] = "watch"
                while time.monotonic() - watch_started < stay:
                    remaining = stay - (time.monotonic() - watch_started)
                    page.wait_for_timeout(int(min(10.0, remaining) * 1000))
                    state = page.evaluate(
                        "(() => { const c=document.querySelector('#canvas'); return {"
                        "connected:document.querySelector('#status-text-pc')?.textContent === '已连接',"
                        "width:c?.width||0,height:c?.height||0}; })()"
                    )
                    samples += 1
                    if not state["connected"] or state["width"] < 320 or state["height"] < 240:
                        raise RuntimeError(f"view interrupted: {state}")
                record["watch_samples"] = samples
                record["phase"] = "complete"
                record["result"] = "PASS"
            except Exception as exc:  # Playwright gives useful typed text here.
                record["result"] = "FAIL"
                record["failure_phase"] = record["phase"]
                record["error"] = str(exc)[:500]
                try:
                    record["failure_state"] = page.evaluate(
                        "(() => { const c=document.querySelector('#canvas'); return {"
                        "path:location.pathname,ready_state:document.readyState,"
                        "status:document.querySelector('#status-text-pc')?.textContent?.trim()||'',"
                        "canvas_width:c?.width||0,canvas_height:c?.height||0}; })()"
                    )
                except Exception:
                    record["failure_state"] = {"unavailable": True}
            finally:
                record["elapsed_s"] = round(time.monotonic() - started, 3)
                record["finished_at"] = utc_now()
                disconnect_started = time.monotonic()
                # page.close() exercises the normal unload path.  Closing the
                # entire context simulates an abrupt browser/client loss.
                if record["abrupt"]:
                    context.close()
                    context = browser.new_context(storage_state=str(args.storage_state))
                else:
                    page.close()
                record["disconnect_ms"] = round(
                    (time.monotonic() - disconnect_started) * 1000, 3
                )
                result.write(json.dumps(record, ensure_ascii=False) + "\n")
                result.flush()
                print(json.dumps(record, ensure_ascii=False), flush=True)
                records.append(record)
        context.close()
        browser.close()
    passed = [record for record in records if record["result"] == "PASS"]
    buckets = {}
    for duration in sorted({record["planned_dwell_s"] for record in records}):
        selected = [record for record in records if record["planned_dwell_s"] == duration]
        successful = [record for record in selected if record["result"] == "PASS"]
        first_frames = [record["first_frame_ms"] for record in successful]
        buckets[str(duration)] = {
            "cycles": len(selected), "passed": len(successful),
            "failed": len(selected) - len(successful),
            "success_percent": round(len(successful) * 100 / len(selected), 2),
            "first_frame_ms_mean": round(statistics.mean(first_frames), 2)
            if first_frames else None,
            "first_frame_ms_p95": percentile(first_frames, 0.95),
        }
    summary = {
        "cycles": cycle_count, "passed": len(passed),
        "failed": cycle_count - len(passed),
        "success_percent": round(len(passed) * 100 / cycle_count, 2),
        "planned_watch_seconds": round(sum(planned_dwells), 2),
        "duration_buckets": buckets,
    }
    summary_path = args.output.with_suffix(args.output.suffix + ".summary.json")
    summary_path.write_text(json.dumps(summary, ensure_ascii=False, indent=2) + "\n",
                            encoding="utf-8")
    print(json.dumps(summary, ensure_ascii=False, indent=2), flush=True)
    return 1 if len(passed) != cycle_count else 0


if __name__ == "__main__":
    raise SystemExit(main())
