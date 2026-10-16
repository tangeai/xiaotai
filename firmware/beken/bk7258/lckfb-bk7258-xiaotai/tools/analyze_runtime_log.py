#!/usr/bin/env python3
"""Summarize BK7258 XiaoTai serial logs and enforce stability gates.

The Beken FreeRTOS port logs ``Task exits abnormally!`` when a task entry
function returns and then deletes that task.  That message is tracked as a
lifecycle warning, but it is deliberately not treated as a CPU crash.  Fault
markers and XiaoTai sessions that stop with a non-zero error remain fatal.
"""

from __future__ import annotations

import argparse
import json
import re
import sys
from dataclasses import dataclass, field
from pathlib import Path


SESSION_START = re.compile(
    r"(?P<mode>AI talk active|VoIP audio active|STREAM audio active).*generation=(?P<generation>\d+)"
)
SESSION_STOP = re.compile(
    r"(?P<mode>AI session finished|VoIP finished|remote STREAM disconnected)"
    r".*generation=(?P<generation>\d+).*error=(?P<error>-?\d+)"
)
METRICS = re.compile(r"METRICS\s+(?P<payload>\{.*\})")
HEARTBEAT_TRACE = re.compile(r"HBTRACE\s+(?P<stage>[a-z0-9-]+(?: rc=-?\d+)?)")
HEARTBEAT_TX = re.compile(
    r"\[DEBUG-bk-hb\] (?P<phase>tx-enter|tx-exit) "
    r"seq=(?P<seq>\d+)(?: rc=(?P<rc>-?\d+))? ms=(?P<ms>\d+)"
)
HEARTBEAT_RX_TIMEOUT = re.compile(
    r"\[DEBUG-bk-hb\] rx-timeout count=(?P<count>\d+)"
)
AP_HARDWARE = re.compile(
    r"\[DEBUG-bk-hb\] ap-hw run=(?P<run>0x[0-9a-fA-F]{8}) "
    r"cpu1=(?P<cpu1>0x[0-9a-fA-F]{8}) "
    r"cpu2=(?P<cpu2>0x[0-9a-fA-F]{8})"
)
AP_STATE = re.compile(
    r"\[DEBUG-bk-hb\] ap-state sample=(?P<sample>[01]) "
    r"tick=(?P<tick>\d+) "
    r"shell=(?P<shell_owner>0x[0-9a-fA-F]{8})/(?P<shell_count>\d+) "
    r"mb=(?P<mb_owner>0x[0-9a-fA-F]{8})/(?P<mb_count>\d+) "
    r"kernel=(?P<kernel_owner>0x[0-9a-fA-F]{8})/(?P<kernel_count>\d+) "
    r"tcb0=(?P<tcb0>0x[0-9a-fA-F]{8}) "
    r"tcb1=(?P<tcb1>0x[0-9a-fA-F]{8})"
)
INVALID_FREE = re.compile(
    r"\[DEBUG-bk-lwip-free\] ptr=(?P<ptr>0x[0-9a-fA-F]+) "
    r"caller=(?P<caller>0x[0-9a-fA-F]+) "
    r"cp_heap=(?P<cp_heap>0x[0-9a-fA-F]+)"
)
RTOS_ASSERT = re.compile(
    r"\[DEBUG-bk-rtos\] api=(?P<api>delay|mutex) "
    r"caller=(?P<caller>0x[0-9a-fA-F]+) "
    r"irq=(?P<irq>[01]) isr=(?P<isr>[01])"
)
MEDIA_STOP = re.compile(
    r"audio stopped up=(?P<up>\d+) down=(?P<down>\d+) dropped=(?P<dropped>\d+)"
    r"(?: aec=(?P<aec>\d+) ref-under=(?P<under>\d+) ref-over=(?P<over>\d+))?"
)
FATAL_MARKERS = (
    "AP crash happend",
    "UsageFault",
    "HardFault",
    "MemManage Fault",
    "BusFault",
    "stack overflow",
)


@dataclass
class Summary:
    lines: int = 0
    sessions_started: dict[str, int] = field(default_factory=dict)
    sessions_stopped: dict[str, int] = field(default_factory=dict)
    nonzero_session_errors: list[dict[str, int | str]] = field(default_factory=list)
    fatal_markers: list[dict[str, int | str]] = field(default_factory=list)
    heartbeat_timeouts: list[dict[str, int | str | None]] = field(default_factory=list)
    invalid_frees: list[dict[str, int | str]] = field(default_factory=list)
    rtos_asserts: list[dict[str, int | str]] = field(default_factory=list)
    task_return_warnings: int = 0
    uplink_not_ready: int = 0
    connection_errors: int = 0
    media_totals: dict[str, int] = field(default_factory=lambda: {
        "up": 0, "down": 0, "dropped": 0, "aec": 0,
        "ref_under": 0, "ref_over": 0,
    })
    metrics_samples: int = 0
    minimum_stack_low_words: int | None = None
    first_ready: dict | None = None
    last_ready: dict | None = None

    def result(self, max_ref_under_percent: float | None,
               max_dropped_bytes: int | None = None,
               min_stack_low_words: int | None = None) -> dict:
        failures: list[str] = []
        if self.fatal_markers:
            failures.append("CPU fault/crash marker present")
        if self.heartbeat_timeouts:
            failures.append("AP IPC heartbeat timeout")
        if self.invalid_frees:
            failures.append("CP lwIP invalid free")
        if self.rtos_asserts:
            failures.append("AP RTOS context assertion")
        if self.nonzero_session_errors:
            failures.append("session ended with non-zero error")

        aec = self.media_totals["aec"]
        under = self.media_totals["ref_under"]
        under_percent = round(under * 100.0 / aec, 2) if aec else None
        if (max_ref_under_percent is not None and under_percent is not None and
                under_percent > max_ref_under_percent):
            failures.append(
                f"AEC reference underflow {under_percent}% exceeds "
                f"{max_ref_under_percent}%"
            )
        dropped = self.media_totals["dropped"]
        if max_dropped_bytes is not None and dropped > max_dropped_bytes:
            failures.append(
                f"audio dropped {dropped} bytes exceeds {max_dropped_bytes}"
            )
        if (min_stack_low_words is not None and
                self.minimum_stack_low_words is not None and
                self.minimum_stack_low_words < min_stack_low_words):
            failures.append(
                f"minimum stack water {self.minimum_stack_low_words} words "
                f"is below {min_stack_low_words}"
            )

        resource_delta = None
        if self.first_ready and self.last_ready:
            resource_delta = {
                "internal_free": (
                    self.last_ready["internal"]["free"] -
                    self.first_ready["internal"]["free"]
                ),
                "psram_free": (
                    self.last_ready["psram"]["free"] -
                    self.first_ready["psram"]["free"]
                ),
                "tasks": self.last_ready["tasks"] - self.first_ready["tasks"],
            }

        return {
            "status": "FAIL" if failures else "PASS",
            "failures": failures,
            "lines": self.lines,
            "sessions": {
                "started": self.sessions_started,
                "stopped": self.sessions_stopped,
                "nonzero_errors": self.nonzero_session_errors,
            },
            "fatal_markers": self.fatal_markers,
            "heartbeat_timeouts": self.heartbeat_timeouts,
            "invalid_frees": self.invalid_frees,
            "rtos_asserts": self.rtos_asserts,
            "lifecycle": {
                "task_return_warnings": self.task_return_warnings,
                "uplink_not_ready": self.uplink_not_ready,
                "connection_errors": self.connection_errors,
            },
            "media": {
                **self.media_totals,
                "ref_under_percent": under_percent,
            },
            "metrics_samples": self.metrics_samples,
            "minimum_stack_low_words": self.minimum_stack_low_words,
            "ready_resource_delta": resource_delta,
        }


def increment(values: dict[str, int], key: str) -> None:
    values[key] = values.get(key, 0) + 1


def analyze(lines: list[str]) -> Summary:
    summary = Summary(lines=len(lines))
    last_stage = None
    last_tx = None
    last_cp_rx = None
    last_ap_hardware = None
    last_ap_state = []
    for number, line in enumerate(lines, start=1):
        if "AP boot reached application entry" in line:
            last_stage = None
            last_tx = None
            last_cp_rx = None
            last_ap_hardware = None
            last_ap_state = []
        trace = HEARTBEAT_TRACE.search(line)
        if trace:
            last_stage = trace.group("stage")
        tx = HEARTBEAT_TX.search(line)
        if tx:
            last_tx = {
                "phase": tx.group("phase"),
                "seq": int(tx.group("seq")),
                "ms": int(tx.group("ms")),
            }
            if tx.group("rc") is not None:
                last_tx["rc"] = int(tx.group("rc"))
        rx_timeout = HEARTBEAT_RX_TIMEOUT.search(line)
        if rx_timeout:
            last_cp_rx = {
                "count": int(rx_timeout.group("count")),
            }
        ap_hardware = AP_HARDWARE.search(line)
        if ap_hardware:
            registers = ap_hardware.groupdict()
            run_status = int(registers["run"], 16)
            last_ap_hardware = {
                **registers,
                "core1_halted": bool(run_status & (1 << 1)),
                "core2_halted": bool(run_status & (1 << 2)),
                "core1_powered_down": bool(run_status & (1 << 9)),
                "core2_powered_down": bool(run_status & (1 << 10)),
            }
        ap_state = AP_STATE.search(line)
        if ap_state:
            fields = ap_state.groupdict()
            sample = {
                "sample": int(fields["sample"]),
                "tick": int(fields["tick"]),
                "tcb0": fields["tcb0"],
                "tcb1": fields["tcb1"],
            }
            for lock in ("shell", "mb", "kernel"):
                sample[lock] = {
                    "owner": fields[f"{lock}_owner"],
                    "count": int(fields[f"{lock}_count"]),
                }
            if sample["sample"] == 0:
                last_ap_state = [sample]
            elif last_ap_state and last_ap_state[0]["sample"] == 0:
                last_ap_state = [last_ap_state[0], sample]
        if "IPC[1]heartbeat timeout" in line:
            timeout = {
                "line": number, "last_stage": last_stage,
            }
            if last_tx is not None:
                timeout["heartbeat_tx"] = last_tx.copy()
            if last_cp_rx is not None:
                timeout["cp_rx"] = last_cp_rx.copy()
            if last_ap_hardware is not None:
                timeout["ap_hardware"] = last_ap_hardware.copy()
            if last_ap_state:
                timeout["ap_state"] = last_ap_state.copy()
            summary.heartbeat_timeouts.append(timeout)
        invalid_free = INVALID_FREE.search(line)
        if invalid_free:
            summary.invalid_frees.append({
                "line": number, **invalid_free.groupdict(),
            })
        rtos_assert = RTOS_ASSERT.search(line)
        if rtos_assert:
            fields = rtos_assert.groupdict()
            summary.rtos_asserts.append({
                "line": number, "api": fields["api"],
                "caller": fields["caller"], "irq": int(fields["irq"]),
                "isr": int(fields["isr"]),
            })
        start = SESSION_START.search(line)
        if start:
            increment(summary.sessions_started, start.group("mode"))
        stop = SESSION_STOP.search(line)
        if stop:
            increment(summary.sessions_stopped, stop.group("mode"))
            error = int(stop.group("error"))
            if error:
                summary.nonzero_session_errors.append({
                    "line": number,
                    "mode": stop.group("mode"),
                    "generation": int(stop.group("generation")),
                    "error": error,
                })
        for marker in FATAL_MARKERS:
            if marker in line:
                summary.fatal_markers.append({"line": number, "marker": marker})
        if "Task exits abnormally!" in line:
            summary.task_return_warnings += 1
        if "uplink not ready" in line:
            summary.uplink_not_ready += 1
        if "connection error=" in line:
            summary.connection_errors += 1

        media = MEDIA_STOP.search(line)
        if media:
            for source, target in (
                ("up", "up"), ("down", "down"), ("dropped", "dropped"),
                ("aec", "aec"), ("under", "ref_under"), ("over", "ref_over"),
            ):
                value = media.group(source)
                if value is not None:
                    summary.media_totals[target] += int(value)

        metrics = METRICS.search(line)
        if metrics:
            try:
                payload = json.loads(metrics.group("payload"))
            except json.JSONDecodeError:
                continue
            summary.metrics_samples += 1
            stack_low_words = payload.get("stack_low_words")
            if isinstance(stack_low_words, int):
                if summary.minimum_stack_low_words is None:
                    summary.minimum_stack_low_words = stack_low_words
                else:
                    summary.minimum_stack_low_words = min(
                        summary.minimum_stack_low_words, stack_low_words
                    )
            if payload.get("state") == "READY":
                summary.first_ready = summary.first_ready or payload
                summary.last_ready = payload
    return summary


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("log", type=Path)
    parser.add_argument("--output", type=Path)
    parser.add_argument(
        "--max-ref-under-percent", type=float,
        help="Optional acoustic gate; omit until a board-specific baseline exists",
    )
    parser.add_argument("--max-dropped-bytes", type=int)
    parser.add_argument("--min-stack-low-words", type=int)
    args = parser.parse_args()
    result = analyze(args.log.read_text(errors="replace").splitlines()).result(
        args.max_ref_under_percent,
        args.max_dropped_bytes,
        args.min_stack_low_words,
    )
    encoded = json.dumps(result, ensure_ascii=False, indent=2) + "\n"
    if args.output:
        args.output.parent.mkdir(parents=True, exist_ok=True)
        args.output.write_text(encoded)
    sys.stdout.write(encoded)
    return 1 if result["status"] == "FAIL" else 0


if __name__ == "__main__":
    raise SystemExit(main())
