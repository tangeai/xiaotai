#!/usr/bin/env python3
"""Exercise a XiaoTai SoftAP portal and record per-cycle JSONL results."""

from __future__ import annotations

import argparse
import http.client
import json
import random
import socket
import statistics
import struct
import time
from datetime import datetime
from pathlib import Path
from typing import Any


PROBES = (
    ("android", "connectivitycheck.gstatic.com", "/generate_204"),
    ("ios", "captive.apple.com", "/hotspot-detect.html"),
    ("windows", "www.msftconnecttest.com", "/connecttest.txt"),
)


def dns_query(name: str, transaction: int) -> bytes:
    labels = name.rstrip(".").split(".")
    encoded = b"".join(bytes((len(label),)) + label.encode("ascii") for label in labels)
    return struct.pack("!HHHHHH", transaction, 0x0100, 1, 0, 0, 0) + encoded + b"\0\0\1\0\1"


def skip_dns_name(packet: bytes, offset: int) -> int:
    while offset < len(packet):
        length = packet[offset]
        offset += 1
        if length == 0:
            return offset
        if length & 0xC0 == 0xC0:
            if offset >= len(packet):
                raise ValueError("truncated DNS pointer")
            return offset + 1
        if length > 63 or offset + length > len(packet):
            raise ValueError("invalid DNS label")
        offset += length
    raise ValueError("unterminated DNS name")


def parse_dns_a(packet: bytes, transaction: int) -> list[str]:
    if len(packet) < 12:
        raise ValueError("short DNS response")
    response_id, flags, questions, answers, _, _ = struct.unpack("!HHHHHH", packet[:12])
    if response_id != transaction or flags & 0x8000 == 0 or flags & 0x000F:
        raise ValueError("invalid DNS response header")
    offset = 12
    for _ in range(questions):
        offset = skip_dns_name(packet, offset)
        offset += 4
        if offset > len(packet):
            raise ValueError("truncated DNS question")
    addresses: list[str] = []
    for _ in range(answers):
        offset = skip_dns_name(packet, offset)
        if offset + 10 > len(packet):
            raise ValueError("truncated DNS answer")
        kind, dns_class, _, length = struct.unpack("!HHIH", packet[offset:offset + 10])
        offset += 10
        if offset + length > len(packet):
            raise ValueError("truncated DNS data")
        if kind == 1 and dns_class == 1 and length == 4:
            addresses.append(socket.inet_ntoa(packet[offset:offset + 4]))
        offset += length
    return addresses


def check_dns(target: str, timeout: float) -> dict[str, Any]:
    transaction = random.SystemRandom().randrange(1, 65536)
    started = time.monotonic()
    with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as client:
        client.settimeout(timeout)
        client.sendto(dns_query("connectivitycheck.gstatic.com", transaction), (target, 53))
        packet, _ = client.recvfrom(1024)
    addresses = parse_dns_a(packet, transaction)
    if target not in addresses:
        raise RuntimeError(f"DNS returned {addresses}, expected {target}")
    return {"ok": True, "latency_ms": round((time.monotonic() - started) * 1000, 2),
            "addresses": addresses}


def http_request(target: str, timeout: float, method: str, path: str,
                 host: str = "192.168.6.1") -> tuple[int, dict[str, str], bytes, float]:
    started = time.monotonic()
    connection = http.client.HTTPConnection(target, 80, timeout=timeout)
    connection.putrequest(method, path, skip_host=True)
    connection.putheader("Host", host)
    connection.putheader("Connection", "close")
    connection.endheaders()
    response = connection.getresponse()
    body = response.read()
    headers = {key.lower(): value for key, value in response.getheaders()}
    status = response.status
    connection.close()
    return status, headers, body, round((time.monotonic() - started) * 1000, 2)


def check_http(target: str, timeout: float) -> dict[str, Any]:
    checks: dict[str, Any] = {}
    status, _, body, latency = http_request(target, timeout, "GET", "/")
    text = body.decode("utf-8", errors="replace")
    if status != 200 or "小钛联网助手" not in text or "选择家里的 Wi-Fi" not in text:
        raise RuntimeError(f"portal root invalid status={status}")
    checks["root"] = {"ok": True, "status": status, "latency_ms": latency,
                      "bytes": len(body)}

    status, _, body, latency = http_request(target, timeout, "GET", "/api/setup")
    setup = json.loads(body)
    if status != 200 or not str(setup.get("ap_ssid", "")).startswith("XiaoTai-"):
        raise RuntimeError(f"setup API invalid status={status} body={setup}")
    checks["setup"] = {"ok": True, "status": status, "latency_ms": latency,
                       "ap_ssid": setup["ap_ssid"]}

    for platform, host, path in PROBES:
        status, headers, _, latency = http_request(target, timeout, "GET", path, host)
        location = headers.get("location")
        if status not in (302, 303) or location != f"http://{target}/":
            raise RuntimeError(
                f"{platform} probe invalid status={status} location={location!r}"
            )
        checks[platform] = {"ok": True, "status": status,
                            "latency_ms": latency, "location": location}
    return checks


def check_scan(target: str, timeout: float) -> dict[str, Any]:
    status, _, _, start_latency = http_request(target, timeout, "POST", "/api/scan")
    if status != 202:
        raise RuntimeError(f"scan start invalid status={status}")
    deadline = time.monotonic() + 12.0
    while time.monotonic() < deadline:
        time.sleep(0.5)
        status, _, body, _ = http_request(target, timeout, "GET", "/api/networks")
        data = json.loads(body)
        if status == 200 and data.get("state") == "ready":
            networks = data.get("networks", [])
            return {"ok": True, "start_latency_ms": start_latency,
                    "networks": len(networks)}
        if data.get("state") == "error":
            break
    raise RuntimeError("Wi-Fi scan did not become ready within 12 seconds")


def metric_summary(metrics_log: Path | None) -> dict[str, Any] | None:
    if metrics_log is None or not metrics_log.exists():
        return None
    samples = []
    for line in metrics_log.read_text(encoding="utf-8", errors="ignore").splitlines():
        marker = line.find("METRICS ")
        if marker < 0:
            continue
        try:
            samples.append(json.loads(line[marker + len("METRICS "):]))
        except json.JSONDecodeError:
            continue
    if not samples:
        return {"samples": 0}
    return {
        "samples": len(samples),
        "internal_free_min": min(item["internal"]["free"] for item in samples),
        "internal_minimum_min": min(item["internal"]["minimum"] for item in samples),
        "psram_free_min": min(item["psram"]["free"] for item in samples),
        "psram_minimum_min": min(item["psram"]["minimum"] for item in samples),
        "cpu_percent_max": max(item.get("cpu_percent", 0) for item in samples),
        "stack_low_words_min": min(item.get("stack_low_words", 0) for item in samples),
    }


def percentile(values: list[float], fraction: float) -> float | None:
    if not values:
        return None
    ordered = sorted(values)
    return ordered[min(len(ordered) - 1, int((len(ordered) - 1) * fraction))]


def main() -> int:
    parser = argparse.ArgumentParser(
        description="Loop XiaoTai AP DNS/HTTP probes and collect JSONL statistics"
    )
    parser.add_argument("--target", default="192.168.6.1")
    parser.add_argument("--cycles", type=int, default=20)
    parser.add_argument("--timeout", type=float, default=3.0)
    parser.add_argument("--dwell-min", type=float, default=1.0)
    parser.add_argument("--dwell-max", type=float, default=5.0)
    parser.add_argument("--include-scan", action="store_true")
    parser.add_argument("--metrics-log", type=Path)
    parser.add_argument("--output", type=Path)
    args = parser.parse_args()
    if args.cycles < 1 or args.dwell_min < 0 or args.dwell_max < args.dwell_min:
        parser.error("invalid cycles or dwell range")
    output = args.output or Path(
        f"/tmp/xiaotai-ap-{datetime.now().strftime('%Y%m%d-%H%M%S')}.jsonl"
    )
    latencies: list[float] = []
    passed = 0
    started = time.time()
    with output.open("w", encoding="utf-8") as result_file:
        for cycle in range(1, args.cycles + 1):
            record: dict[str, Any] = {"cycle": cycle, "timestamp": time.time(),
                                      "target": args.target, "ok": False}
            cycle_started = time.monotonic()
            try:
                record["dns"] = check_dns(args.target, args.timeout)
                record["http"] = check_http(args.target, args.timeout)
                if args.include_scan:
                    record["scan"] = check_scan(args.target, args.timeout)
                record["ok"] = True
                passed += 1
            except Exception as error:  # record failures and continue the stability run
                record["error"] = f"{type(error).__name__}: {error}"
            record["cycle_ms"] = round((time.monotonic() - cycle_started) * 1000, 2)
            latencies.append(record["cycle_ms"])
            result_file.write(json.dumps(record, ensure_ascii=False) + "\n")
            result_file.flush()
            print(f"cycle={cycle}/{args.cycles} ok={record['ok']} ms={record['cycle_ms']}"
                  + (f" error={record.get('error')}" if not record["ok"] else ""))
            if cycle < args.cycles:
                time.sleep(random.uniform(args.dwell_min, args.dwell_max))
    summary = {
        "target": args.target,
        "cycles": args.cycles,
        "passed": passed,
        "failed": args.cycles - passed,
        "success_percent": round(passed * 100.0 / args.cycles, 2),
        "elapsed_seconds": round(time.time() - started, 2),
        "cycle_ms_mean": round(statistics.mean(latencies), 2),
        "cycle_ms_p50": percentile(latencies, 0.50),
        "cycle_ms_p95": percentile(latencies, 0.95),
        "cycle_ms_max": max(latencies),
        "device_metrics": metric_summary(args.metrics_log),
        "jsonl": str(output),
    }
    summary_path = output.with_suffix(output.suffix + ".summary.json")
    summary_path.write_text(json.dumps(summary, ensure_ascii=False, indent=2) + "\n",
                            encoding="utf-8")
    print(json.dumps(summary, ensure_ascii=False, indent=2))
    print(f"summary={summary_path}")
    return 0 if passed == args.cycles else 1


if __name__ == "__main__":
    raise SystemExit(main())
